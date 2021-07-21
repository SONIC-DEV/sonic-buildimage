/*
 * IR359x driver for power and compatibles
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
#include "i2c_dev_sysfs.h"

//#define DEBUG

#ifdef DEBUG
#define IR3595_DEBUG(fmt, ...) do {                   \
  printk(KERN_DEBUG "%s:%d " fmt "\n",            \
         __FUNCTION__, __LINE__, ##__VA_ARGS__);  \
} while (0)

#else /* !DEBUG */

#define IR3595_DEBUG(fmt, ...)
#endif


#define TO_IR3595_DATA(x)  container_of(x, struct ir3595_data_t, dev_data)
#define SYSFS_READ 0
#define SYSFS_WRITE 1
#define LABEL_NAME_MAX 50
enum chips {
	IR3595 = 1,
};

enum alarm {
	IN0_MIN = 1,
	IN0_MAX,
	CURR1_MIN,
	CURR1_MAX,
};


struct alarm_data_t {
	int alarm_min;
	int alarm_max;
};

struct ir3595_alarm_data {
	struct alarm_data_t in0; //Vout
	struct alarm_data_t curr1;
	char in0_label[LABEL_NAME_MAX + 1];
	char curr1_label[LABEL_NAME_MAX + 1];
};


struct ir3595_data_t {
	i2c_dev_data_st dev_data;
	struct ir3595_alarm_data alarm_data;
};


static const struct i2c_device_id ir3595_id[] = {
	{"ir3595", IR3595 },

	{ }
};

static ssize_t ir3595_vout_show(struct device *dev,
                                    struct device_attribute *attr,
                                    char *buf)
{
	int value = -1;
	int count = 10;
	
	while((value < 0 || value == 0xffff) && count--) {
		value = i2c_dev_read_word_bigendian(dev, attr);
	}
	
	if (value < 0) {
		IR3595_DEBUG("I2C read error, return %d!\n", value);
		return value;
	}
	
	value = (value * 1000) / 2048;
	
	return scnprintf(buf, PAGE_SIZE, "%d\n", value);
}

static ssize_t ir3595_iout_show(struct device *dev,
                                    struct device_attribute *attr,
                                    char *buf)
{
	int value = -1;
	int count = 10;
	
	while((value < 0 || value == 0xff) && count--) {
		value = i2c_dev_read_byte(dev, attr);
	}
	
	if (value < 0) {
		IR3595_DEBUG("I2C read error, return %d!\n", value);
		return value;
	}
	
	value = value * 2 * 1000;
	
	return scnprintf(buf, PAGE_SIZE, "%d\n", value);
}

static int alarm_value_rw(struct ir3595_data_t *data, int reg, int opcode, int value)
{
	int *p = NULL;

	switch(reg) {
		case IN0_MIN:
			p = &data->alarm_data.in0.alarm_min;
			break;
		case IN0_MAX:
			p = &data->alarm_data.in0.alarm_max;
			break;
		case CURR1_MIN:
			p = &data->alarm_data.curr1.alarm_min;
			break;
		case CURR1_MAX:
			p = &data->alarm_data.curr1.alarm_max;
			break;
		default:
			return -ENOTSUPP;
	}

	if(opcode == SYSFS_READ)
		return *p;
	else if(opcode == SYSFS_WRITE)
		*p = value;
	else
		return -EACCES;

	return 0;
}

static char *alarm_label_rw(struct ir3595_data_t *data, int reg, int opcode, char *name)
{
	int len;
	char *p = NULL;

	switch(reg) {
		case IN0_MIN:
		case IN0_MAX:
			p = data->alarm_data.in0_label;
			break;
		case CURR1_MIN:
		case CURR1_MAX:
			p = data->alarm_data.curr1_label;
			break;
		default:
			return NULL;
	}

	if(opcode == SYSFS_READ)
		return p;
	else if(opcode == SYSFS_WRITE) {
		len = (strlen(name) > LABEL_NAME_MAX) ? LABEL_NAME_MAX : strlen(name);
		memcpy(p, name, len);
	} else
		return NULL;

	return NULL;
}

static ssize_t ir3595_alarm_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	int value = -1;
	struct i2c_client *client = to_i2c_client(dev);
	i2c_dev_data_st *data = i2c_get_clientdata(client);
	i2c_sysfs_attr_st *i2c_attr = TO_I2C_SYSFS_ATTR(attr);
	const i2c_dev_attr_st *dev_attr = i2c_attr->isa_i2c_attr;
	struct ir3595_data_t *ir3595_data = TO_IR3595_DATA(data);
	struct ir3595_alarm_data alarm_data;

	if(!ir3595_data)
		return -ENXIO;

	value = alarm_value_rw(ir3595_data, dev_attr->ida_reg, SYSFS_READ, 0);

	return scnprintf(buf, PAGE_SIZE, "%d\n", value);
}

static ssize_t ir3595_alarm_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
	int rc;
	long write_value = 0;
	struct i2c_client *client = to_i2c_client(dev);
	i2c_dev_data_st *data = i2c_get_clientdata(client);
	i2c_sysfs_attr_st *i2c_attr = TO_I2C_SYSFS_ATTR(attr);
	const i2c_dev_attr_st *dev_attr = i2c_attr->isa_i2c_attr;
	struct ir3595_data_t *ir3595_data = TO_IR3595_DATA(data);
	struct ir3595_alarm_data alarm_data;

	if(!ir3595_data)
		return -1;

	if (buf == NULL) {
		return -ENXIO;
	}

	rc = kstrtol(buf, 10, &write_value);
	if (rc != 0)	{
		return count;
	}
	rc = alarm_value_rw(ir3595_data, dev_attr->ida_reg, SYSFS_WRITE, write_value);
	if(rc < 0)
		return rc;

	return count;
}

static ssize_t ir358x_label_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
	char *label;
	struct i2c_client *client = to_i2c_client(dev);
	i2c_dev_data_st *data = i2c_get_clientdata(client);
	i2c_sysfs_attr_st *i2c_attr = TO_I2C_SYSFS_ATTR(attr);
	const i2c_dev_attr_st *dev_attr = i2c_attr->isa_i2c_attr;
	struct ir3595_data_t *ir3595_data = TO_IR3595_DATA(data);
	struct ir3595_alarm_data alarm_data;

	if(!ir3595_data)
		return -ENXIO;

	label = alarm_label_rw(ir3595_data, dev_attr->ida_reg, SYSFS_READ, NULL);

	return scnprintf(buf, PAGE_SIZE, "%s\n", label);
}

static ssize_t ir358x_label_store(struct device *dev,
        struct device_attribute *attr, const char *buf, size_t count)
{
	int rc, len;
	char name[LABEL_NAME_MAX + 1];
	struct i2c_client *client = to_i2c_client(dev);
	i2c_dev_data_st *data = i2c_get_clientdata(client);
	i2c_sysfs_attr_st *i2c_attr = TO_I2C_SYSFS_ATTR(attr);
	const i2c_dev_attr_st *dev_attr = i2c_attr->isa_i2c_attr;
	struct ir3595_data_t *ir3595_data = TO_IR3595_DATA(data);
	struct ir3595_alarm_data alarm_data;

	if(!ir3595_data)
		return -1;

	if (buf == NULL) {
		return -ENXIO;
	}

	len = (strlen(buf) > LABEL_NAME_MAX) ? LABEL_NAME_MAX : strlen(buf);
	snprintf(name, len, "%s", buf);
	name[len] = '\0';
	rc = alarm_label_rw(ir3595_data, dev_attr->ida_reg, SYSFS_WRITE, name);
	if(rc < 0)
		return rc;

	return count;
}

static const i2c_dev_attr_st ir3595_attr_table[] = {
	{
	  "in0_input",
	  NULL,
	  ir3595_vout_show,
	  NULL,
	  0x9a, 0, 8,
	},
	{
	  "curr1_input",
	  NULL,
	  ir3595_iout_show,
	  NULL,
	  0x94, 0, 8,
	},
	{
	  "in0_label",
	  "Switch chip Voltage",
	  ir358x_label_show,
	  ir358x_label_store,
	  IN0_MIN, 0, 0,
	},
	{
	  "curr1_label",
	  "Switch chip Current",
	  ir358x_label_show,
	  ir358x_label_store,
	  CURR1_MIN, 0, 0,
	},
	{
	  "in0_min",
	  NULL,
	  ir3595_alarm_show,
	  ir3595_alarm_store,
	  IN0_MIN, 0, 0,
	},
	{
	  "in0_max",
	  NULL,
	  ir3595_alarm_show,
	  ir3595_alarm_store,
	  IN0_MAX, 0, 0,
	},
	{
	  "curr1_min",
	  NULL,
	  ir3595_alarm_show,
	  ir3595_alarm_store,
	  CURR1_MIN, 0, 0,
	},
	{
	  "curr1_max",
	  NULL,
	  ir3595_alarm_show,
	  ir3595_alarm_store,
	  CURR1_MAX, 0, 0,
	},

};

static int ir3595_remove(struct i2c_client *client)
{
	i2c_dev_data_st *data = i2c_get_clientdata(client);
	struct ir3595_data_t *ir3595_data = TO_IR3595_DATA(data);

	i2c_dev_sysfs_data_clean(client, &ir3595_data->dev_data);
	kfree(ir3595_data);
	return 0;
}

static int ir3595_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int n_attrs;
	struct ir3595_data_t *data;

	//client->flags |= I2C_CLIENT_PEC;
	if (!i2c_check_functionality(client->adapter,
			I2C_FUNC_SMBUS_READ_BYTE | I2C_FUNC_SMBUS_READ_BYTE_DATA
			| I2C_FUNC_SMBUS_READ_WORD_DATA))
		return -ENODEV;

	n_attrs = sizeof(ir3595_attr_table) / sizeof(ir3595_attr_table[0]);

	data = devm_kzalloc(&client->dev, sizeof(struct ir3595_data_t), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	return i2c_dev_sysfs_data_init(client, &data->dev_data, ir3595_attr_table, n_attrs);
}


static struct i2c_driver ir3595_driver = {
	.driver = {
		   .name = "ir3595",
		   },
	.probe = ir3595_probe,
	.remove = ir3595_remove,
	.id_table = ir3595_id,
};

module_i2c_driver(ir3595_driver);

MODULE_AUTHOR("Micky Zhan@Celestica.com");
MODULE_DESCRIPTION("FRU driver for PSU");
MODULE_LICENSE("GPL");

