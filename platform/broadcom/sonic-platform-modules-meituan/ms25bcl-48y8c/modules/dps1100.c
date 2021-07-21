/*
 * Hardware monitoring driver for DPS1100 and compatibles
 * Based on the pfe3000 driver with the following copyright:
 *
 * Copyright (c) 2011 Ericsson AB.
 * Copyright (c) 2012 Guenter Roeck
 * Copyright 2004-present Facebook. All Rights Reserved.
 * Copyright 2018 Celestica.
 *
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */



#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/ktime.h>
#include <linux/delay.h>
#include <linux/i2c/pmbus.h>

#include "pmbus.h"

#define SYSFS_READ 0
#define SYSFS_WRITE 1

#define DPS1100_PSU_NUM 4

#define DPS1100_OP_REG_ADDR     PMBUS_OPERATION
#define DPS1100_OP_SHUTDOWN_CMD 0x0
#define DPS1100_OP_POWERON_CMD  0x80
#define DPS1100_FAN1_PWM_REG 0x3B
#define DPS1100_FAN1_SPEED_REG 0x90


#define DPS1100_WAIT_TIME		1000	/* uS	*/

#define TO_DPS1100_DATA(x)  container_of(x, struct dps1100_data, info)
#define TO_PMBUS_DATA(x)  container_of(x, struct pmbus_data, hwmon_dev)
#define TO_I2C_DEV(x)  container_of(x, struct device, driver_data)
#define TO_I2C_SYSFS_ATTR(_attr) container_of(_attr, struct sysfs_attr_t, dev_attr)

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


typedef ssize_t (*i2c_dev_attr_show_fn)(struct device *dev, struct device_attribute *attr, char *buf);
typedef ssize_t (*i2c_dev_attr_store_fn)(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);

#define I2C_DEV_ATTR_SHOW_DEFAULT (i2c_dev_attr_show_fn)(1)
#define I2C_DEV_ATTR_STORE_DEFAULT (i2c_dev_attr_store_fn)(1)


struct pmbus_data {
	struct device *dev;
	struct device *hwmon_dev;

	u32 flags;		/* from platform data */

	/* linear mode: exponent for output voltages */
	int exponent[PMBUS_PAGES];

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


enum chips {
	DPS550 = 1,
	DPS1100,
};


struct alarm_data_t {
	int alarm_min;
	int alarm_max;
};

struct dps1100_alarm_data {
	struct alarm_data_t in1; //VIN
	struct alarm_data_t in2; //VOUT
	struct alarm_data_t fan1;
	struct alarm_data_t temp1;
	struct alarm_data_t temp2;
	struct alarm_data_t pin;
	struct alarm_data_t pout;
	struct alarm_data_t iin;
	struct alarm_data_t iout;
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

struct dps1100_data {
	int id;
	int shutdown_state;
	int fan1_speed;
	int fan1_pct;
	struct i2c_client *client;
	struct dps1100_alarm_data alarm_data;
	struct pmbus_driver_info info;
	struct attribute_group attr_group;
	struct sysfs_attr_t *sysfs_attr;
};

struct dps1100_vin_threshold_t {
	int bus;
	int vin_min;
	int vin_max;
};

static const struct i2c_device_id dps1100_id[] = {
	{"dps550", DPS550 },
	{"dps1100", DPS1100 },
	{ }
};
struct dps1100_vin_threshold_t dps1100_vin_threshold[DPS1100_PSU_NUM];

enum PSU {
	PSU1 = 1,
	PSU2,
};
/*
 * 0 is not OK, 1 is OK, error codes if fails.
 */
extern int psu_ok(int bus, unsigned short addr);
static ssize_t dps1100_ok(struct i2c_client *client)
{
	if (client == NULL)
		return 0;

	// FIXME: This adapter number pass to the the function is not a good
	//        idea, it can be dynamically assigned and cause problem.
	//        see syscpld.c:psu_ok()
	return psu_ok(client->adapter->nr, client->addr);
}

static ssize_t dps1100_shutdown_show(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
	u8 read_val = 0;
	struct i2c_client *client = to_i2c_client(dev);
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct dps1100_data *data = TO_DPS1100_DATA(info);

	if (dps1100_ok(client) != 1)
		return -1;

	//client->flags |= I2C_CLIENT_PEC;
	read_val = pmbus_read_byte_data(client, 0, DPS1100_OP_REG_ADDR);
	if (read_val >= 0)
	{
		if (read_val == DPS1100_OP_SHUTDOWN_CMD)
			data->shutdown_state = 1;
		else
			data->shutdown_state = 0;
	}

	return sprintf(buf, "%d\n", data->shutdown_state);
}

static ssize_t dps1100_shutdown_store(struct device *dev,
                                      struct device_attribute *attr,
                                      const char *buf, size_t count)
{
	u8 write_value = 0;
	long shutdown = 0;
	int rc = 0;
	struct i2c_client *client = to_i2c_client(dev);
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct dps1100_data *data = TO_DPS1100_DATA(info);

	if (dps1100_ok(client) != 1)
		return -1;

	//client->flags |= I2C_CLIENT_PEC;
	if (buf == NULL) {
		return -ENXIO;
	}

	rc = kstrtol(buf, 0, &shutdown);
	if (rc != 0)	{
		return count;
	}

	if (shutdown == 1) {
		write_value = DPS1100_OP_SHUTDOWN_CMD;
	} else {
		write_value = DPS1100_OP_POWERON_CMD;
	}

	rc = pmbus_write_byte_data(client, 0, DPS1100_OP_REG_ADDR, write_value);
	if (rc == 0) {
		data->shutdown_state = 1;
	}

	return count;
}

static ssize_t dps1100_reg_byte_show(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
	int read_val = 0;
	struct pmbus_data *pdata = dev_get_drvdata(dev);
	struct i2c_client *client = to_i2c_client(pdata->dev);
	struct sysfs_attr_t *sysfs_attr = TO_I2C_SYSFS_ATTR(attr);
	struct i2c_dev_attr_t *dev_attr = sysfs_attr->i2c_attr;

	if (dps1100_ok(client) != 1)
		return -1;

	read_val = pmbus_read_byte_data(client, 0, dev_attr->reg);
	if (read_val < 0)
		return read_val;

	return sprintf(buf, "%d\n", read_val);
}

static ssize_t dps1100_reg_byte_store(struct device *dev,
                                      struct device_attribute *attr,
                                      const char *buf, size_t count)
{
	int rc = 0;
	u8 write_value = 0;
	struct pmbus_data *pdata = dev_get_drvdata(dev);
	struct i2c_client *client = to_i2c_client(pdata->dev);
	struct sysfs_attr_t *sysfs_attr = TO_I2C_SYSFS_ATTR(attr);
	struct i2c_dev_attr_t *dev_attr = sysfs_attr->i2c_attr;

	if (dps1100_ok(client) != 1)
		return -1;

	//client->flags |= I2C_CLIENT_PEC;
	if (buf == NULL) {
		return -ENXIO;
	}

	rc = kstrtou8(buf, 0, &write_value);
	if (rc != 0)	{
		return count;
	}

	rc = pmbus_write_byte_data(client, 0, dev_attr->reg, write_value);
	if (rc < 0) {
		return rc;
	}

	return count;
}



static ssize_t dps1100_vin_threshold_show(struct device *dev,
					  struct device_attribute *attr, 
					  char *buf)
{
	int i;
	int read_val = 0;
	int bus = -1;
	struct pmbus_data *pdata = dev_get_drvdata(dev);
	struct i2c_client *client = to_i2c_client(pdata->dev);
	struct sysfs_attr_t *sysfs_attr = TO_I2C_SYSFS_ATTR(attr);
	struct i2c_dev_attr_t *dev_attr = sysfs_attr->i2c_attr;

	if (client->adapter)
		bus = client->adapter->nr;

	if (bus < 0)
		return -1;

	for (i = 0; i < DPS1100_PSU_NUM; i++) {
		if (dps1100_vin_threshold[i].bus == bus) {
			if (dev_attr->reg == 0)
				read_val = dps1100_vin_threshold[i].vin_min;
			else
				read_val = dps1100_vin_threshold[i].vin_max;
			break;
		}
	}

	return sprintf(buf, "%d\n", read_val);
}

static ssize_t dps1100_vin_threshold_store(struct device *dev,
                		           struct device_attribute *attr,
                		           const char *buf, size_t count)
{
	int i;
	int rc = 0;
	int write_value = 0;
	int bus = -1;
	struct pmbus_data *pdata = dev_get_drvdata(dev);
	struct i2c_client *client = to_i2c_client(pdata->dev);
	struct sysfs_attr_t *sysfs_attr = TO_I2C_SYSFS_ATTR(attr);
	struct i2c_dev_attr_t *dev_attr = sysfs_attr->i2c_attr;


	if (buf == NULL) {
		return -ENXIO;
	}

	if (client->adapter)
		bus = client->adapter->nr;

	if (bus < 0)
		return -1;

	rc = kstrtoint(buf, 0, &write_value);
	if (rc != 0)	{
		return count;
	}

	for (i = 0; i < DPS1100_PSU_NUM; i++) {
		if (dps1100_vin_threshold[i].bus == bus) {
			if (dev_attr->reg == 0)
				dps1100_vin_threshold[i].vin_min = write_value;
			else
				dps1100_vin_threshold[i].vin_max = write_value;
			return count;
		}
	}
	for (i = 0; i < DPS1100_PSU_NUM; i++) {
		if (dps1100_vin_threshold[i].bus == 0) {
			if (dev_attr->reg == 0)
				dps1100_vin_threshold[i].vin_min = write_value;
			else
				dps1100_vin_threshold[i].vin_max = write_value;
			dps1100_vin_threshold[i].bus = bus;
			break;
		}
	}

	return count;
}


static ssize_t dps1100_reg_word_show(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
	int read_val = 0;
	struct pmbus_data *pdata = dev_get_drvdata(dev);
	struct i2c_client *client = to_i2c_client(pdata->dev);
	struct sysfs_attr_t *sysfs_attr = TO_I2C_SYSFS_ATTR(attr);
	struct i2c_dev_attr_t *dev_attr = sysfs_attr->i2c_attr;

	if (dps1100_ok(client) != 1)
		return -1;

	read_val = pmbus_read_word_data(client, 0, dev_attr->reg);
	if (read_val < 0)
	{
		return read_val;
	}

	return sprintf(buf, "%d\n", read_val);
}

static ssize_t dps1100_reg_word_store(struct device *dev,
                                      struct device_attribute *attr,
                                      const char *buf, size_t count)
{
	int rc = 0;
	u16 write_value = 0;
	struct pmbus_data *pdata = dev_get_drvdata(dev);
	struct i2c_client *client = to_i2c_client(pdata->dev);
	struct sysfs_attr_t *sysfs_attr = TO_I2C_SYSFS_ATTR(attr);
	struct i2c_dev_attr_t *dev_attr = sysfs_attr->i2c_attr;

	if (dps1100_ok(client) != 1)
		return -1;

	//client->flags |= I2C_CLIENT_PEC;
	if (buf == NULL) {
		return -ENXIO;
	}

	rc = kstrtou16(buf, 0, &write_value);
	if (rc != 0)	{
		return count;
	}

	rc = pmbus_write_word_data(client, 0, dev_attr->reg, write_value);
	if (rc < 0) {
		return rc;
	}

	return count;
}

static struct i2c_dev_attr_t psu_attr_table[] = {
	{
		"in1_min",
		NULL,
		dps1100_vin_threshold_show,
		dps1100_vin_threshold_store,
		0,
	},
	{
		"in1_max",
		NULL,
		dps1100_vin_threshold_show,
		dps1100_vin_threshold_store,
		1,
	},
	{
		"in2_min",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0,
	},
	{
		"in2_max",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0,
	},
	{
		"fan1_min",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0,
	},
	{
		"fan1_max",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0,
	},
	{
		"temp1_max_hyst",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0,
	},
	{
		"temp1_max",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0,
	},
	{
		"temp2_max_hyst",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0,
	},
	{
		"temp2_max",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0,
	},
	{
		"power1_min",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0,
	},
	{
		"power1_max",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0,
	},
	{
		"power2_min",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0,
	},
	{
		"power2_max",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0,
	},
	{
		"curr1_min",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0,
	},
	{
		"curr1_max",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0,
	},
	{
		"curr2_min",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0,
	},
	{
		"curr2_max",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0,
	},
	{
		"fan1_cfg",
		NULL,
		dps1100_reg_byte_show,
		dps1100_reg_byte_store,
		0x3a,
	},
	{
		"fan1_pct",
		NULL,
		dps1100_reg_word_show,
		dps1100_reg_word_store,
		0x3b,
	},

};


static struct pmbus_platform_data platform_data = {
	.flags = PMBUS_SKIP_STATUS_CHECK,
};

static int sysfs_value_rw(unsigned int *reg, int opcode, int val)
{
	if (opcode == SYSFS_READ)
		return *reg;
	else if (opcode == SYSFS_WRITE)
		*reg = val;
	else
		return -EINVAL;

	return 0;
}
static ssize_t i2c_dev_sysfs_show(struct device *dev,
                                  struct device_attribute *attr, char *buf)
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

	ret = kstrtouint(buf, 0, &val);
	if (ret != 0)	{
		return -EINVAL;
	}

	ret = sysfs_value_rw(&dev_attr->reg, SYSFS_WRITE, val);
	if (ret < 0) {
		return ret;
	}

	return count;
}


static int i2c_dev_sysfs_data_clean(struct device *dev, struct dps1100_data *data)
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

static int dps1100_register_sysfs(struct device *dev,
                                  struct dps1100_data *data,
                                  struct i2c_dev_attr_t *dev_attrs,
                                  int n_attrs)
{
	int i;
	int ret;
	mode_t mode;
	struct sysfs_attr_t *cur_attr;
	struct i2c_dev_attr_t *cur_dev_attr;
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
	for (i = 0; i < n_attrs; i++, cur_attr++, cur_grp_attr++, cur_dev_attr++) {
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
	if (ret < 0) {
		goto exit_cleanup;
	}

	return 0;
exit_cleanup:
	i2c_dev_sysfs_data_clean(dev, data);
	return ret;
}

static void dps1100_remove_sysfs(struct i2c_client *client)
{
	struct pmbus_data *pdata = (struct pmbus_data *)i2c_get_clientdata(client);
	const struct pmbus_driver_info *info = pmbus_get_driver_info(client);
	struct dps1100_data *data = TO_DPS1100_DATA(info);

	i2c_dev_sysfs_data_clean(pdata->hwmon_dev, data);
	return;
}

static DEVICE_ATTR(shutdown, S_IRUGO | S_IWUSR,
                   NULL, dps1100_shutdown_store);

static struct attribute *shutdown_attrs[] = {
	&dev_attr_shutdown.attr,
	NULL
};
static struct attribute_group control_attr_group = {
	.name = "control",
	.attrs = shutdown_attrs,
};


static int dps1100_register_shutdown(struct i2c_client *client,
                                     const struct i2c_device_id *id)
{
	return sysfs_create_group(&client->dev.kobj, &control_attr_group);
}

static void dps1100_remove_shutdown(struct i2c_client *client)
{
	sysfs_remove_group(&client->dev.kobj, &control_attr_group);
	return;
}

static int dps1100_remove(struct i2c_client *client)
{
	dps1100_remove_shutdown(client);
	dps1100_remove_sysfs(client);
	return pmbus_do_remove(client);
}

static int dps1100_pmbus_read_word_data(struct i2c_client *client, int page, int reg)
{
	int ret;

	/*
	 * This mask out the auto probe sensor limits,
	 * Since we want to use our custom limits.
	 */
	if (reg >= PMBUS_VIRT_BASE
		|| reg == PMBUS_VIN_UV_WARN_LIMIT
		|| reg == PMBUS_VIN_UV_FAULT_LIMIT
		|| reg == PMBUS_VIN_OV_WARN_LIMIT
		|| reg == PMBUS_VIN_OV_FAULT_LIMIT
		|| reg == PMBUS_VOUT_UV_WARN_LIMIT
		|| reg == PMBUS_VOUT_UV_FAULT_LIMIT
		|| reg == PMBUS_VOUT_OV_FAULT_LIMIT
		|| reg == PMBUS_VOUT_OV_WARN_LIMIT
		|| reg == PMBUS_IIN_OC_WARN_LIMIT
		|| reg == PMBUS_IIN_OC_FAULT_LIMIT
		|| reg == PMBUS_IOUT_OC_WARN_LIMIT
		|| reg == PMBUS_IOUT_UC_FAULT_LIMIT
		|| reg == PMBUS_IOUT_OC_FAULT_LIMIT
		|| reg == PMBUS_PIN_OP_WARN_LIMIT
		|| reg == PMBUS_POUT_MAX
		|| reg == PMBUS_POUT_OP_WARN_LIMIT
		|| reg == PMBUS_POUT_OP_FAULT_LIMIT
		|| reg == PMBUS_UT_WARN_LIMIT
		|| reg == PMBUS_UT_FAULT_LIMIT
		|| reg == PMBUS_OT_WARN_LIMIT
		|| reg == PMBUS_OT_FAULT_LIMIT)
		return -ENXIO;

	if (dps1100_ok(client) != 1)
		return -1;
	ret = pmbus_read_word_data(client, page, reg);

	return ret;
}

static int dps1100_pmbus_write_word_data(struct i2c_client *client, int page, int reg, u16 word)
{
	int ret;

	if (dps1100_ok(client) != 1)
		return -1;

	ret = pmbus_write_word_data(client, page, reg, word);

	return ret;
}

static int dps1100_pmbus_read_byte_data(struct i2c_client *client, int page, int reg)
{
	int ret;

	if (dps1100_ok(client) != 1)
		return -1;

	ret = pmbus_read_byte_data(client, page, reg);

	return ret;
}

static int dps1100_pmbus_write_byte(struct i2c_client *client, int page, u8 value)
{
	int ret;

	if (dps1100_ok(client) != 1)
		return -1;

	ret = pmbus_write_byte(client, page, value);

	return ret;
}

static int dps1100_probe(struct i2c_client *client,
                         const struct i2c_device_id *id)
{
	int ret = 0;
	int n_attrs;
	struct dps1100_data *data;
	struct pmbus_driver_info *info;
	struct pmbus_data *pdata;

	if (!i2c_check_functionality(client->adapter,
	                             I2C_FUNC_SMBUS_READ_WORD_DATA | I2C_FUNC_SMBUS_READ_BLOCK_DATA))
		return -ENODEV;

	data = devm_kzalloc(&client->dev, sizeof(struct dps1100_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->shutdown_state = 0;
	data->client = client;

	info = &data->info;
	info->pages = 1;

	info->func[0] = PMBUS_HAVE_VIN
	                | PMBUS_HAVE_VOUT
	                | PMBUS_HAVE_IOUT
	                | PMBUS_HAVE_TEMP
	                | PMBUS_HAVE_PIN
	                | PMBUS_HAVE_POUT
	                | PMBUS_HAVE_FAN12
	                | PMBUS_HAVE_IIN
	                | PMBUS_HAVE_TEMP2;

	info->read_word_data = dps1100_pmbus_read_word_data;
	info->write_word_data = dps1100_pmbus_write_word_data;
	info->read_byte_data = dps1100_pmbus_read_byte_data;
	info->write_byte = dps1100_pmbus_write_byte;

	ret = pmbus_do_probe(client, id, info);
	if (ret < 0) {
		dev_err(&client->dev, "pmbus probe error\n");
		return -EIO;
	}
	pdata = (struct pmbus_data *)i2c_get_clientdata(client);
	if (pdata) {
		n_attrs = sizeof(psu_attr_table) / sizeof(psu_attr_table[0]);
		ret = dps1100_register_sysfs(pdata->hwmon_dev, data, psu_attr_table, n_attrs);
		if (ret < 0) {
			dev_err(&client->dev, "Unsupported alarm sysfs operation\n");
			return -EIO;
		}
	}

	ret = dps1100_register_shutdown(client, id);
	if (ret < 0) {
		dev_err(&client->dev, "Unsupported shutdown operation\n");
		return -EIO;
	}

	return 0;
}


static struct i2c_driver dps1100_driver = {
	.driver = {
		.name = "dps1100",
	},
	.probe = dps1100_probe,
	.remove = dps1100_remove,
	.id_table = dps1100_id,
};

module_i2c_driver(dps1100_driver);

MODULE_AUTHOR("Micky Zhan, based on work by Guenter Roeck");
MODULE_DESCRIPTION("PMBus driver for DPS1100");
MODULE_VERSION("0.0.3");
MODULE_LICENSE("GPL");
