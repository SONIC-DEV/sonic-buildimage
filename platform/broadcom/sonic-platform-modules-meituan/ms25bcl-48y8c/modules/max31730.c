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
#include <linux/i2c.h>
#include <linux/ktime.h>
#include <linux/delay.h>

#include "i2c_dev_sysfs.h"

#define MAX31730_ALARM_NODE 0x0
#define SYSFS_READ 0
#define SYSFS_WRITE 1
#define Get_Bit(val, n)		((1 << n) & val)

struct temp_data {
	int max;
	int max_hyst;
};

struct temp_data *local_temp_limit;
struct temp_data *remote1_temp_limit;
struct temp_data *remote2_temp_limit;

static i2c_dev_data_st *max31730_data;

enum chips {
	MAX31730,
};

static const struct i2c_device_id max31730_id[] = {
	{"max31730", MAX31730},
};

static const int frac_tab[4] = { 5000, 2500, 1250, 625 };
static long register2floatvalue(unsigned char msb,unsigned char lsb)
{
	int sign = msb & 0x80 ? -1 : 1;
	long fraction = 0;
	int exponent = (msb & 0x7f) * 10000;
	int i = 4;
	for(; i < 8; i++)
	{
		fraction += (Get_Bit(lsb, i) ? frac_tab[7 - i] : 0);
	}

	return sign * (fraction + exponent);
}

static ssize_t max31730_show(struct device *dev,
                                    struct device_attribute *attr,
                                    char *buf)
{
	long value = 0;
	unsigned char values[2] = { 0, 0 };
	int ret_val;

	ret_val = i2c_dev_read_nbytes(dev, attr, values, 2);
  if (ret_val < 0) {
		printk(KERN_INFO "i2c_dev_read_nbytes failed! \n");
		return ret_val;
	}
	// values[0] : MSB
	// values[1] : LSB
	value = register2floatvalue(values[0], values[1]);

	return scnprintf(buf, PAGE_SIZE, "%d\n", value / 10);
}

static int temp_value_rw(const char *name, int opcode, int value)
{
	int *p = NULL;

	if(strcmp(name, "temp1_max") == 0) {
		p = &local_temp_limit->max;
	} else if(strcmp(name, "temp1_max_hyst") == 0) {
		p = &local_temp_limit->max_hyst;
	} else if(strcmp(name, "temp2_max") == 0) {
		p = &remote1_temp_limit->max;
	} else if(strcmp(name, "temp2_max_hyst") == 0) {
		p = &remote1_temp_limit->max_hyst;
	} else if(strcmp(name, "temp3_max") == 0) {
		p = &remote2_temp_limit->max;
	} else if(strcmp(name, "temp3_max_hyst") == 0) {
		p = &remote2_temp_limit->max_hyst;
	} else {
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

static ssize_t max31730_alarm_show(struct device *dev,
                                    struct device_attribute *attr,
                                    char *buf)
{
	int value = -1;
	i2c_sysfs_attr_st *i2c_attr = TO_I2C_SYSFS_ATTR(attr);
	const i2c_dev_attr_st *dev_attr = i2c_attr->isa_i2c_attr;
	const char *name = dev_attr->ida_name;

	if(!name)
		return -EINVAL;

	value = temp_value_rw(name, SYSFS_READ, 0);

	return scnprintf(buf, PAGE_SIZE, "%d\n", value);
}

static ssize_t max31730_alarm_store(struct device *dev,
                                struct device_attribute *attr,
                                const char *buf, size_t count)
{
	int rc;
	long write_value = 0;
	i2c_sysfs_attr_st *i2c_attr = TO_I2C_SYSFS_ATTR(attr);
	const i2c_dev_attr_st *dev_attr = i2c_attr->isa_i2c_attr;
	const char *name = dev_attr->ida_name;

	if(!name)
		return -EINVAL;

	if (buf == NULL) {
		return -ENXIO;
	}

	rc = kstrtol(buf, 10, &write_value);
	if (rc != 0)	{
		return count;
	}
	rc = temp_value_rw(name, SYSFS_WRITE, write_value);
	if(rc < 0)
		return rc;

	return count;
}

static const i2c_dev_attr_st max31730_attr_table[] = {
	{
	  "temp1_input",
	  "local temprature\n",
	  max31730_show,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x00, 0, 8,
	},
	{
	  "temp1_max",
	  "local temprature max\n",
	  max31730_alarm_show,
	  max31730_alarm_store,
	  MAX31730_ALARM_NODE, 0, 8,
	},
	{
	  "temp1_max_hyst",
	  "local hysterical temprature\n",
	  max31730_alarm_show,
	  max31730_alarm_store,
	  MAX31730_ALARM_NODE, 0, 8,
	},
	{
	  "temp2_input",
	  "remote 1 temprature\n",
	  max31730_show,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x02, 0, 8,
	},
		{
	  "temp2_max",
	  "remote1 temprature max\n",
	  max31730_alarm_show,
	  max31730_alarm_store,
	  MAX31730_ALARM_NODE, 0, 8,
	},
	{
	  "temp2_max_hyst",
	  "remote1 hysterical temprature\n",
	  max31730_alarm_show,
	  max31730_alarm_store,
	  MAX31730_ALARM_NODE, 0, 8,
	},
	{
	  "temp3_input",
	  "remote 2 temprature\n",
	  max31730_show,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x04, 0, 8,
	},
		{
	  "temp3_max",
	  "remote2 temprature max\n",
	  max31730_alarm_show,
	  max31730_alarm_store,
	  MAX31730_ALARM_NODE, 0, 8,
	},
	{
	  "temp3_max_hyst",
	  "remote2 hysterical temprature\n",
	  max31730_alarm_show,
	  max31730_alarm_store,
	  MAX31730_ALARM_NODE, 0, 8,
	},
	{
	  "remote_3_temp",
	  "remote 3 temprature\n",
	  max31730_show,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x06, 0, 8,
	},
	{
	  "highest_temp",
	  "highest temprature\n",
	  max31730_show,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x10, 0, 8,
	},
	{
	  "highest_temp_en",
	  "highest temprature enable\n"
	  "bit0: local\n"
	  "bit1: remote 1\n"
	  "bit2: remote 2\n"
	  "bit3: remote 3\n"
	  "value:\n"
	  "0x0: disable\n"
	  "0x1: enable",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x12, 0, 8,
	},
	{
	  "configuration",
	  "configuration\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x13, 0, 8,
	},
	{
	  "customer_ideality_factor",
	  "customer ideality factor\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x14, 0, 8,
	},
	{
	  "customer_ideality_factor_en",
	  "customer ideality factor enable\n"
	  "bit1: remote 1\n"
	  "bit2: remote 2\n"
	  "bit3: remote 3\n"
	  "value:\n"
	  "0x0: disable\n"
	  "0x1: enable",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x15, 0, 8,
	},
	{
	  "customer_offset",
	  "customer offset\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x16, 0, 8,
	},
	{
	  "customer_offset_en",
	  "customer offset enable\n"
	  "0x0: disable\n"
	  "0x1 enable",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x17, 0, 8,
	},
	{
	  "filter_en",
	  "filter enable:\n"
	  "(should be disabled when not in constant conversion mode)\n"
	  "0x0: disable\n"
	  "0x1 enable",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x18, 0, 8,
	},
	{
	  "beta_compensation_en",
	  "beta compensation enable:\n"
	  "0x0: disable\n"
	  "0x1 enable",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x19, 0, 8,
	},
	{
	  "beta_val_ch1",
	  "beta value of channel 1\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  NULL,
	  0x1a, 0, 8,
	},
	{
	  "beta_val_ch2",
	  "beta value of channel 2\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  NULL,
	  0x1b, 0, 8,
	},
	{
	  "beta_val_ch3",
	  "beta value of channel 3\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  NULL,
	  0x1c, 0, 8,
	},
	{
	  "local_th_h_limit",
	  "local thermal high limit\n",
	  max31730_show,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x20, 0, 8,
	},
	{
	  "remote_1_th_h_limit",
	  "remote 1 thermal high limit\n",
	  max31730_show,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x22, 0, 8,
	},
	{
	  "remote_2_th_h_limit",
	  "remote 2 thermal high limit\n",
	  max31730_show,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x24, 0, 8,
	},
	{
	  "remote_3_th_h_limit",
	  "remote 3 thermal high limit\n",
	  max31730_show,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x26, 0, 8,
	},
	{
	  "th_l_limit",
	  "thermal low limit\n",
	  max31730_show,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x30, 0, 8,
	},
	{
	  "th_h_status",
	  "thermal status, high temperature\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  NULL,
	  0x32, 0, 8,
	},
	{
	  "th_l_status",
	  "thermal status, low temperature\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  NULL,
	  0x33, 0, 8,
	},
	{
	  "THERM_N_mask",
	  "mask faults from asserting the THERM pin for each channel\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x34, 0, 8,
	},
	{
	  "temp_ch_en",
	  "temperature channel enable\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x35, 0, 8,
	},
	{
	  "diode_fault_status",
	  "diode fault status\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  NULL,
	  0x36, 0, 8,
	},
	{
	  "local_ref_temp",
	  "local reference temperature\n",
	  max31730_show,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x40, 0, 8,
	},
	{
	  "remote_1_ref_temp",
	  "remote 1 reference temperature\n",
	  max31730_show,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x42, 0, 8,
	},
	{
	  "remote_2_ref_temp",
	  "remote 2 reference temperature\n",
	  max31730_show,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x44, 0, 8,
	},
	{
	  "remote_3_ref_temp",
	  "remote 3 reference temperature\n",
	  max31730_show,
	  I2C_DEV_ATTR_STORE_DEFAULT,
	  0x46, 0, 8,
	},
	{
	  "mfr_id",
	  "manufacture id\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  NULL,
	  0x50, 0, 8,
	},
	{
	  "revision",
	  "die revision code\n",
	  I2C_DEV_ATTR_SHOW_DEFAULT,
	  NULL,
	  0x51, 0, 8,
	},
};

static int max31730_remove(struct i2c_client *client)
{
	i2c_dev_sysfs_data_clean(client, max31730_data);
	kfree(max31730_data);
	if(local_temp_limit)
		kfree(local_temp_limit);
	if(remote1_temp_limit)
		kfree(remote1_temp_limit);
	if(remote2_temp_limit)
		kfree(remote2_temp_limit);
	return 0;
}

static int max31730_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int n_attrs;

	if (!i2c_check_functionality(client->adapter,
			I2C_FUNC_SMBUS_READ_BYTE | I2C_FUNC_SMBUS_READ_BYTE_DATA))
		return -ENODEV;

	n_attrs = sizeof(max31730_attr_table) / sizeof(max31730_attr_table[0]);

	max31730_data = devm_kzalloc(&client->dev, sizeof(i2c_dev_data_st), GFP_KERNEL);
	if (!max31730_data)
		return -ENOMEM;

	local_temp_limit = kzalloc(sizeof(struct temp_data), GFP_KERNEL);
	if(!local_temp_limit)
		goto release1;
	remote1_temp_limit = kzalloc(sizeof(struct temp_data), GFP_KERNEL);
	if(!remote1_temp_limit)
		goto release2;
	remote2_temp_limit = kzalloc(sizeof(struct temp_data), GFP_KERNEL);
	if(!remote2_temp_limit)
		goto release3;

	return i2c_dev_sysfs_data_init(client, max31730_data, max31730_attr_table, n_attrs);

release3:
	if(remote1_temp_limit)
		kfree(remote1_temp_limit);
release2:
	if(local_temp_limit)
		kfree(local_temp_limit);
release1:
	return -ENOMEM;
}

static struct i2c_driver max31730_driver = {
	.driver = {
		   .name = "max31730",
		   },
	.probe = max31730_probe,
	.remove = max31730_remove,
	.id_table = max31730_id,
};

module_i2c_driver(max31730_driver);

MODULE_AUTHOR("Tianhui Xu <tianhui@celestica.com>");
MODULE_DESCRIPTION("I2C driver for Maxim integrated MAX31730");
MODULE_LICENSE("GPL");
