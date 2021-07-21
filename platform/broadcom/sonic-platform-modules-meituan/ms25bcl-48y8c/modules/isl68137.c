/*
 * Hardware monitoring driver for Intersil ISL68137
 *
 * Copyright (c) 2018 Celestica Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/i2c/pmbus.h>
#include "pmbus.h"

//#define DEBUG

#ifdef DEBUG
#define ISL68137_DEBUG(fmt, ...) do {                   \
  printk(KERN_DEBUG "%s:%d " fmt "\n",            \
         __FUNCTION__, __LINE__, ##__VA_ARGS__);  \
} while (0)

#else /* !DEBUG */

#define ISL68137_DEBUG(fmt, ...)
#endif

#define TO_I2C_SYSFS_ATTR(_attr) container_of(_attr, struct sysfs_attr_t, dev_attr)
#define TO_ISL68137_DATA(x)  container_of(x, struct isl68137_data_t, info)
#define ISL68137_ALARM_NODE 0x1
#define SYSFS_READ 0
#define SYSFS_WRITE 1

typedef ssize_t (*i2c_dev_attr_show_fn)(struct device *dev, struct device_attribute *attr, char *buf);
typedef ssize_t (*i2c_dev_attr_store_fn)(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);

#define I2C_DEV_ATTR_SHOW_DEFAULT (i2c_dev_attr_show_fn)(1)
#define I2C_DEV_ATTR_STORE_DEFAULT (i2c_dev_attr_store_fn)(1)

enum alarm {
	IIN_MIN = 1,
	IIN_MAX,
	IOUT2_MIN,
	IOUT2_MAX,
	VIN_MIN,
	VIN_MAX,
	VOUT2_MIN,
	VOUT2_MAX,
	PIN_MIN,
	PIN_MAX,
	POUT2_MIN,
	POUT2_MAX,
	TEMP_MAX_HYST,
	TEMP_MAX,
};

struct temp_data_t {
	int max;
	int max_hyst;
};

struct alarm_data_t {
	int alarm_min;
	int alarm_max;
};

struct isl68137_alarm_data_t {
	struct alarm_data_t iin;
	struct alarm_data_t iout2;
	struct alarm_data_t vin;
	struct alarm_data_t vout2;
	struct alarm_data_t pin;
	struct alarm_data_t pout2;
	struct temp_data_t  temp;
};

struct isl68137_data_t {
	int id;
	struct isl68137_alarm_data_t alarm_data;
	struct pmbus_driver_info info;
	struct attribute_group attr_group;
	struct sysfs_attr_t *sysfs_attr;
};

struct i2c_dev_attr_t {
  const char *name;
  const char *help;
  i2c_dev_attr_show_fn show;
  i2c_dev_attr_store_fn store;
  int reg;
} ;

struct sysfs_attr_t {
	struct device_attribute dev_attr;
	struct i2c_dev_attr_t *i2c_attr;
};

/*
 * Index into status register array, per status register group
 */
#define PB_STATUS_BASE		0
#define PB_STATUS_VOUT_BASE	(PB_STATUS_BASE + PMBUS_PAGES)
#define PB_STATUS_IOUT_BASE	(PB_STATUS_VOUT_BASE + PMBUS_PAGES)
#define PB_STATUS_FAN_BASE	(PB_STATUS_IOUT_BASE + PMBUS_PAGES)
#define PB_STATUS_FAN34_BASE	(PB_STATUS_FAN_BASE + PMBUS_PAGES)
#define PB_STATUS_TEMP_BASE	(PB_STATUS_FAN34_BASE + PMBUS_PAGES)
#define PB_STATUS_INPUT_BASE	(PB_STATUS_TEMP_BASE + PMBUS_PAGES)
#define PB_STATUS_VMON_BASE	(PB_STATUS_INPUT_BASE + 1)
#define PB_NUM_STATUS_REG	(PB_STATUS_VMON_BASE + 1)

struct pmbus_data {
	struct device *dev;
	struct device *hwmon_dev;

	u32 flags;		/* from platform data */

	int exponent[PMBUS_PAGES];
				/* linear mode: exponent for output voltages */

	const struct pmbus_driver_info *info;

	int max_attributes;
	int num_attributes;
	struct attribute_group group;
	const struct attribute_group *groups[2];

	struct pmbus_sensor *sensors;

	struct mutex update_lock;
	bool valid;
	unsigned long last_updated;	/* in jiffies */

	ktime_t access;

	/*
	 * A single status register covers multiple attributes,
	 * so we keep them all together.
	 */
	u8 status[PB_NUM_STATUS_REG];
	u8 status_register;

	u8 currpage;
};

static struct pmbus_driver_info isl68137_info = {
	.pages = 2,
	.format[PSC_VOLTAGE_IN] = direct,
	.format[PSC_VOLTAGE_OUT] = direct,
	.format[PSC_CURRENT_IN] = direct,
	.format[PSC_CURRENT_OUT] = direct,
	.format[PSC_POWER] = direct,
	.format[PSC_TEMPERATURE] = direct,
	.m[PSC_VOLTAGE_IN] = 1,
	.b[PSC_VOLTAGE_IN] = 0,
	.R[PSC_VOLTAGE_IN] = 3,
	.m[PSC_VOLTAGE_OUT] = 1,
	.b[PSC_VOLTAGE_OUT] = 0,
	.R[PSC_VOLTAGE_OUT] = 3,
	.m[PSC_CURRENT_IN] = 1,
	.b[PSC_CURRENT_IN] = 0,
	.R[PSC_CURRENT_IN] = 2,
	.m[PSC_CURRENT_OUT] = 1,
	.b[PSC_CURRENT_OUT] = 0,
	.R[PSC_CURRENT_OUT] = 1,
	.m[PSC_POWER] = 1,
	.b[PSC_POWER] = 0,
	.R[PSC_POWER] = -3,
	.m[PSC_TEMPERATURE] = 1,
	.b[PSC_TEMPERATURE] = 0,
	.R[PSC_TEMPERATURE] = 0,
	.func[0] = PMBUS_HAVE_VIN | PMBUS_HAVE_IIN | PMBUS_HAVE_PIN
		| PMBUS_HAVE_STATUS_INPUT,
	.func[1] = PMBUS_HAVE_VIN | PMBUS_HAVE_IIN | PMBUS_HAVE_PIN
		| PMBUS_HAVE_STATUS_INPUT | PMBUS_HAVE_TEMP | PMBUS_HAVE_STATUS_TEMP
		| PMBUS_HAVE_VOUT | PMBUS_HAVE_STATUS_VOUT
		| PMBUS_HAVE_IOUT | PMBUS_HAVE_STATUS_IOUT | PMBUS_HAVE_POUT,
};

static const struct i2c_dev_attr_t isl68137_attr_table[] = {
	{
	  "curr1_min",
	  "iin min value\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  IIN_MIN, 0, 8,
	},
	{
	  "curr1_max",
	  "iin max value\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  IIN_MAX, 0, 8,
	},
	{
	  "curr2_min",
	  "iout2 min value\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  IOUT2_MIN, 0, 8,
	},
	{
	  "curr2_max",
	  "iout2 max value\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  IOUT2_MAX, 0, 8,
	},
	{
	  "in1_min",
	  "vin min value\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  VIN_MIN, 0, 8,
	},
	{
	  "in1_max",
	  "vin max value\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  VIN_MAX, 0, 8,
	},
	{
	  "in2_min",
	  "vout2 min value\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  VOUT2_MIN, 0, 8,
	},
	{
	  "in2_max",
	  "vout2 max value\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  VOUT2_MAX, 0, 8,
	},
	{
	  "power1_min",
	  "pin min value\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  PIN_MIN, 0, 8,
	},
	{
	  "power1_max",
	  "pin max value\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  PIN_MAX, 0, 8,
	},
	{
	  "power2_min",
	  "pout2 min value\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  POUT2_MIN, 0, 8,
	},
	{
	  "power2_max",
	  "pout2 max value\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  POUT2_MAX, 0, 8,
	},
	{
	  "temp1_max",
	  "temp1 temprature max value\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  TEMP_MAX, 0, 8,
	},
	{
	  "temp1_max_hyst",
	  "temp1 hysterical temprature\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  TEMP_MAX_HYST, 0, 8,
	},
};

static int sysfs_value_rw(int *reg, int opcode, int val)
{
	if(opcode == SYSFS_READ)
		return *reg;
	else if(opcode == SYSFS_WRITE)
		*reg = val;
	else
		return -1;

	return 0;
}

static ssize_t i2c_dev_sysfs_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int val;
	struct sysfs_attr_t *sysfs_attr = TO_I2C_SYSFS_ATTR(attr);
	struct i2c_dev_attr_t *dev_attr = sysfs_attr->i2c_attr;

	if (!dev_attr->show) {
		return -EOPNOTSUPP;
	}

	if (dev_attr->show != I2C_DEV_ATTR_SHOW_DEFAULT) {
		return dev_attr->show(dev, attr, buf);
	}
	val = sysfs_value_rw(&dev_attr->reg, SYSFS_READ, 0);
	if (val < 0) {
		return val;
	}

	return sprintf(buf, "%d\n", val);
}

static ssize_t i2c_dev_sysfs_store(struct device *dev,
                                   struct device_attribute *attr,
                                   const char *buf, size_t count)
{
	int val;
	int ret;
	struct sysfs_attr_t *sysfs_attr = TO_I2C_SYSFS_ATTR(attr);
	struct i2c_dev_attr_t *dev_attr = sysfs_attr->i2c_attr;

	if (!dev_attr->store) {
		return -EOPNOTSUPP;
	}

	if (dev_attr->store != I2C_DEV_ATTR_STORE_DEFAULT) {
		return dev_attr->store(dev, attr, buf, count);
	}

	/* parse the buffer */
	if (sscanf(buf, "%i", &val) <= 0) {
		return -EINVAL;
	}
	ret = sysfs_value_rw(&dev_attr->reg, SYSFS_WRITE, val);
	if (ret < 0) {
		return ret;
	}

	return count;
}

static int i2c_dev_sysfs_data_clean(struct device *dev, struct isl68137_data_t *data)
{
	if (!data) {
		return 0;
	}

	if (data->attr_group.attrs) {
		sysfs_remove_group(&dev->kobj, &data->attr_group);
		kfree(data->attr_group.attrs);
	}
	if (data->sysfs_attr) {
		kfree(data->sysfs_attr);
	}

	return 0;
}

static int isl68137_register_sysfs(struct device *dev,
			struct isl68137_data_t *data, const struct i2c_dev_attr_t *dev_attrs, int n_attrs)
{
	int i;
	int ret;
	mode_t mode;
	struct sysfs_attr_t *cur_attr;
	const struct i2c_dev_attr_t *cur_dev_attr;
	struct attribute **cur_grp_attr;

	data->sysfs_attr = kzalloc(sizeof(*data->sysfs_attr) * n_attrs, GFP_KERNEL);
	data->attr_group.attrs = kzalloc(sizeof(*data->attr_group.attrs) * (n_attrs + 1), GFP_KERNEL);
	if (!data->sysfs_attr || !data->attr_group.attrs) {
		ret = -ENOMEM;
		goto exit_cleanup;
	}

	cur_attr = &data->sysfs_attr[0];
	cur_grp_attr = &data->attr_group.attrs[0];
	cur_dev_attr = dev_attrs;
	for(i = 0; i < n_attrs; i++, cur_attr++, cur_grp_attr++, cur_dev_attr++) {
		mode = S_IRUGO;
		if (cur_dev_attr->store) {
			mode |= S_IWUSR;
		}
		cur_attr->dev_attr.attr.name = cur_dev_attr->name;
		cur_attr->dev_attr.attr.mode = mode;
		cur_attr->dev_attr.show = i2c_dev_sysfs_show;
		cur_attr->dev_attr.store = i2c_dev_sysfs_store;
		*cur_grp_attr = &cur_attr->dev_attr.attr;
		cur_attr->i2c_attr = cur_dev_attr;
	}

	ret = sysfs_create_group(&dev->kobj, &data->attr_group);
	if(ret < 0) {
		goto exit_cleanup;
	}

	return 0;
exit_cleanup:
	i2c_dev_sysfs_data_clean(dev, data);
	return ret;
}

static void isl68137_remove_sysfs(struct i2c_client *client)
{
	struct pmbus_data *pdata = (struct pmbus_data *)i2c_get_clientdata(client);
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct isl68137_data_t *data = TO_ISL68137_DATA(info);

	i2c_dev_sysfs_data_clean(pdata->hwmon_dev, data);
	return;
}

static int isl68137_remove(struct i2c_client *client)
{
	isl68137_remove_sysfs(client);
	pmbus_do_remove(client);
	return 0;
}

static int isl68137_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	int n_attrs;
	struct isl68137_data_t *data;
	struct pmbus_data *pdata;

	if (!i2c_check_functionality(client->adapter,
			I2C_FUNC_SMBUS_READ_BYTE | I2C_FUNC_SMBUS_READ_BYTE_DATA
			| I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -ENODEV;

	data = devm_kzalloc(&client->dev, sizeof(struct isl68137_data_t), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	int ret = pmbus_do_probe(client, id, &isl68137_info);
	if(ret < 0)
		return -EIO;

	pdata = (struct pmbus_data *)i2c_get_clientdata(client);
	if(pdata) {
		n_attrs = sizeof(isl68137_attr_table) / sizeof(isl68137_attr_table[0]);
		ret = isl68137_register_sysfs(pdata->hwmon_dev, data, isl68137_attr_table, n_attrs);
		if(ret < 0) {
			dev_err(&client->dev, "Unsupported alarm sysfs operation\n");
			return -EIO;
		}
	}

	return 0;
}

static const struct i2c_device_id isl68137_id[] = {
	{"isl68137", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, isl68137_id);

/* This is the driver that will be inserted */
static struct i2c_driver isl68137_driver = {
	.driver = {
		.name = "isl68137",
	},
	.probe = isl68137_probe,
	.remove = isl68137_remove,
	.id_table = isl68137_id,
};

module_i2c_driver(isl68137_driver);

MODULE_AUTHOR("Tianhui Xu <tianhui@celestica.com>");
MODULE_DESCRIPTION("PMBus driver for Intersil ISL68137");
MODULE_LICENSE("GPL");
