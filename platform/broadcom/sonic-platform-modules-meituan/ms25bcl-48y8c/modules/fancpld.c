/*
 * FAN CPLD driver for CPLD and compatibles
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
#include "i2c_dev_sysfs.h"

#define DEBUG

#define FANCPLD_ALARM_NODE 0xff /*just for flag using*/
#define SYSFS_READ 0
#define SYSFS_WRITE 1

#ifdef DEBUG
#define FANCPLD_DEBUG(fmt, ...) do {              \
  printk(KERN_DEBUG "%s:%d " fmt "\n",            \
         __FUNCTION__, __LINE__, ##__VA_ARGS__);  \
} while (0)

#else /* !DEBUG */

#define FANCPLD_DEBUG(fmt, ...)
#endif

struct alarm_data_t {
	int alarm_min;
	int alarm_max;
};

struct ir358x_alarm_data {
	struct alarm_data_t fan1;
	struct alarm_data_t fan2;
	struct alarm_data_t fan3;
	struct alarm_data_t fan4;
	struct alarm_data_t fan5;
	struct alarm_data_t fan6;
	struct alarm_data_t fan7;
	struct alarm_data_t fan8;
	struct alarm_data_t fan9;
	struct alarm_data_t fan10;
};

static i2c_dev_data_st fancpld_data;
struct ir358x_alarm_data fancpld_alarm_data;


static int alarm_value_rw(const char *name, int opcode, int value)
{
	int *p = NULL;

	if (strcmp(name, "fan1_min") == 0) {
		p = &fancpld_alarm_data.fan1.alarm_min;
	} else if (strcmp(name, "fan1_max") == 0) {
		p = &fancpld_alarm_data.fan1.alarm_max;
	} else if (strcmp(name, "fan2_min") == 0) {
		p = &fancpld_alarm_data.fan2.alarm_min;
	} else if (strcmp(name, "fan2_max") == 0) {
		p = &fancpld_alarm_data.fan2.alarm_max;
	} else if (strcmp(name, "fan3_min") == 0) {
		p = &fancpld_alarm_data.fan3.alarm_min;
	} else if (strcmp(name, "fan3_max") == 0) {
		p = &fancpld_alarm_data.fan3.alarm_max;
	} else if (strcmp(name, "fan4_min") == 0) {
		p = &fancpld_alarm_data.fan4.alarm_min;
	} else if (strcmp(name, "fan4_max") == 0) {
		p = &fancpld_alarm_data.fan4.alarm_max;
	} else if (strcmp(name, "fan5_min") == 0) {
		p = &fancpld_alarm_data.fan5.alarm_min;
	} else if (strcmp(name, "fan5_max") == 0) {
		p = &fancpld_alarm_data.fan5.alarm_max;
	} else if (strcmp(name, "fan6_min") == 0) {
		p = &fancpld_alarm_data.fan6.alarm_min;
	} else if (strcmp(name, "fan6_max") == 0) {
		p = &fancpld_alarm_data.fan6.alarm_max;
	} else if (strcmp(name, "fan7_min") == 0) {
		p = &fancpld_alarm_data.fan7.alarm_min;
	} else if (strcmp(name, "fan7_max") == 0) {
		p = &fancpld_alarm_data.fan7.alarm_max;
	} else if (strcmp(name, "fan8_min") == 0) {
		p = &fancpld_alarm_data.fan8.alarm_min;
	} else if (strcmp(name, "fan8_max") == 0) {
		p = &fancpld_alarm_data.fan8.alarm_max;
	} else if (strcmp(name, "fan9_min") == 0) {
		p = &fancpld_alarm_data.fan9.alarm_min;
	} else if (strcmp(name, "fan9_max") == 0) {
		p = &fancpld_alarm_data.fan9.alarm_max;
	} else if (strcmp(name, "fan10_min") == 0) {
		p = &fancpld_alarm_data.fan10.alarm_min;
	} else if (strcmp(name, "fan10_max") == 0) {
		p = &fancpld_alarm_data.fan10.alarm_max;
	} else {
		return -1;
	}

	if (opcode == SYSFS_READ)
		return *p;
	else if (opcode == SYSFS_WRITE)
		*p = value;
	else
		return -1;

	return 0;
}


static ssize_t fan_rpm_show(struct device *dev,
                            struct device_attribute *attr,
                            char *buf)
{
	int value = -1;

	value = i2c_dev_read_byte(dev, attr);

	if (value < 0) {
		FANCPLD_DEBUG("I2C read error!\n");
		return -1;
	}


	return scnprintf(buf, PAGE_SIZE, "%d\n", value * 150);
}

static ssize_t fan_alarm_show(struct device *dev,
                              struct device_attribute *attr,
                              char *buf)
{
	int value = -1;
	i2c_sysfs_attr_st *i2c_attr = TO_I2C_SYSFS_ATTR(attr);
	const i2c_dev_attr_st *dev_attr = i2c_attr->isa_i2c_attr;
	const char *name = dev_attr->ida_name;

	if (!name)
		return -1;

	value = alarm_value_rw(name, SYSFS_READ, 0);

	return scnprintf(buf, PAGE_SIZE, "%d\n", value);
}

static ssize_t fan_alarm_store(struct device *dev,
                               struct device_attribute *attr,
                               const char *buf, size_t count)
{
	int rc;
	int write_value = 0;
	i2c_sysfs_attr_st *i2c_attr = TO_I2C_SYSFS_ATTR(attr);
	const i2c_dev_attr_st *dev_attr = i2c_attr->isa_i2c_attr;
	const char *name = dev_attr->ida_name;

	if (!name)
		return -1;

	if (buf == NULL) {
		return -ENXIO;
	}

	rc = kstrtoint(buf, 10, &write_value);
	if (rc != 0)	{
		return count;
	}
	rc = alarm_value_rw(name, SYSFS_WRITE, write_value);
	if (rc < 0)
		return -1;

	return count;
}



enum chips {
	FANCPLD = 1,
};


static const struct i2c_device_id fancpld_id[] = {
	{"fancpld", FANCPLD },
	{ }
};


static const i2c_dev_attr_st fancpld_attr_table[] = {
	{
		"version",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x0, 0, 8,
	},
	{
		"scratch",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x4, 0, 8,
	},
	{
		"int_status",
		"0x0: interrupt\n"
		"0x1: not interrupt",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x6, 0, 1,
	},
	{
		"int_result",
		"0x0: not interrupt\n"
		"0x1: interrupt",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x7, 0, 1,
	},
	{
		"eeprom_wp",
		"0x0: protect\n"
		"0x1: not protect",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x9, 0, 1,
	},
	{
		"wdt_en",
		"0x0: fan wdt disable\n"
		"0x1: fan wdt enable",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0xc, 0, 1,
	},
	{
		"wdt_status",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0xc, 4, 1,
	},
	{
		"fan1_input",
		NULL,
		fan_rpm_show,
		NULL,
		0x20, 0, 8,
	},
	{
		"fan2_input",
		NULL,
		fan_rpm_show,
		NULL,
		0x21, 0, 8,
	},
	{
		"fan1_pwm",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x22, 0, 8,
	},
	{
		"fan1_led",
		"0x1: green\n"
		"0x2: red\n"
		"0x3: off",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x24, 0, 2,
	},
	{
		"fan1_eeprom_wp",
		"0x0: protect\n"
		"0x1: not protect",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x25, 0, 1,
	},
	{
		"fan1_present",
		"0x0: present\n"
		"0x1: absent",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x26, 0, 1,
	},
	{
		"fan1_dir",
		"0x0: F2B\n"
		"0x1: B2F",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x26, 1, 1,
	},
	{
		"fan3_input",
		NULL,
		fan_rpm_show,
		NULL,
		0x40, 0, 8,
	},
	{
		"fan4_input",
		NULL,
		fan_rpm_show,
		NULL,
		0x41, 0, 8,
	},
	{
		"fan2_pwm",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x42, 0, 8,
	},
	{
		"fan2_led",
		"0x1: green\n"
		"0x2: red\n"
		"0x3: off",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x44, 0, 2,
	},
	{
		"fan2_eeprom_wp",
		"0x0: protect\n"
		"0x1: not protect",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x45, 0, 1,
	},
	{
		"fan2_present",
		"0x0: present\n"
		"0x1: absent",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x46, 0, 1,
	},
	{
		"fan2_dir",
		"0x0: F2B\n"
		"0x1: B2F",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x46, 1, 1,
	},
	{
		"fan5_input",
		NULL,
		fan_rpm_show,
		NULL,
		0x60, 0, 8,
	},
	{
		"fan6_input",
		NULL,
		fan_rpm_show,
		NULL,
		0x61, 0, 8,
	},
	{
		"fan3_pwm",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x62, 0, 8,
	},
	{
		"fan3_led",
		"0x1: green\n"
		"0x2: red\n"
		"0x3: off",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x64, 0, 2,
	},
	{
		"fan3_eeprom_wp",
		"0x0: protect\n"
		"0x1: not protect",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x65, 0, 1,
	},
	{
		"fan3_present",
		"0x0: present\n"
		"0x1: absent",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x66, 0, 1,
	},
	{
		"fan3_dir",
		"0x0: F2B\n"
		"0x1: B2F",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x66, 1, 1,
	},
	{
		"fan7_input",
		NULL,
		fan_rpm_show,
		NULL,
		0x80, 0, 8,
	},
	{
		"fan8_input",
		NULL,
		fan_rpm_show,
		NULL,
		0x81, 0, 8,
	},
	{
		"fan4_pwm",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x82, 0, 8,
	},
	{
		"fan4_led",
		"0x1: green\n"
		"0x2: red\n"
		"0x3: off",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x84, 0, 2,
	},
	{
		"fan4_eeprom_wp",
		"0x0: protect\n"
		"0x1: not protect",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x85, 0, 1,
	},
	{
		"fan4_present",
		"0x0: present\n"
		"0x1: absent",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x86, 0, 1,
	},
	{
		"fan4_dir",
		"0x0: F2B\n"
		"0x1: B2F",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x86, 1, 1,
	},
	{
		"fan9_input",
		NULL,
		fan_rpm_show,
		NULL,
		0xa0, 0, 8,
	},
	{
		"fan10_input",
		NULL,
		fan_rpm_show,
		NULL,
		0xa1, 0, 8,
	},
	{
		"fan5_pwm",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0xa2, 0, 8,
	},
	{
		"fan5_led",
		"0x1: green\n"
		"0x2: red\n"
		"0x3: off",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0xa4, 0, 2,
	},
	{
		"fan5_eeprom_wp",
		"0x0: protect\n"
		"0x1: not protect",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0xa5, 0, 1,
	},
	{
		"fan5_present",
		"0x0: present\n"
		"0x1: absent",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0xa6, 0, 1,
	},
	{
		"fan5_dir",
		"0x0: F2B\n"
		"0x1: B2F",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0xa6, 1, 1,
	},
	{
		"fan1_min",
		NULL,
		fan_alarm_show,
		fan_alarm_store,
		FANCPLD_ALARM_NODE, 0, 0,
	},
	{
		"fan1_max",
		NULL,
		fan_alarm_show,
		fan_alarm_store,
		FANCPLD_ALARM_NODE, 0, 0,
	},
	{
		"fan2_min",
		NULL,
		fan_alarm_show,
		fan_alarm_store,
		FANCPLD_ALARM_NODE, 0, 0,
	},
	{
		"fan2_max",
		NULL,
		fan_alarm_show,
		fan_alarm_store,
		FANCPLD_ALARM_NODE, 0, 0,
	},
	{
		"fan3_min",
		NULL,
		fan_alarm_show,
		fan_alarm_store,
		FANCPLD_ALARM_NODE, 0, 0,
	},
	{
		"fan3_max",
		NULL,
		fan_alarm_show,
		fan_alarm_store,
		FANCPLD_ALARM_NODE, 0, 0,
	},
	{
		"fan4_min",
		NULL,
		fan_alarm_show,
		fan_alarm_store,
		FANCPLD_ALARM_NODE, 0, 0,
	},
	{
		"fan4_max",
		NULL,
		fan_alarm_show,
		fan_alarm_store,
		FANCPLD_ALARM_NODE, 0, 0,
	},
	{
		"fan5_min",
		NULL,
		fan_alarm_show,
		fan_alarm_store,
		FANCPLD_ALARM_NODE, 0, 0,
	},
	{
		"fan5_max",
		NULL,
		fan_alarm_show,
		fan_alarm_store,
		FANCPLD_ALARM_NODE, 0, 0,
	},
	{
		"fan6_min",
		NULL,
		fan_alarm_show,
		fan_alarm_store,
		FANCPLD_ALARM_NODE, 0, 0,
	},
	{
		"fan6_max",
		NULL,
		fan_alarm_show,
		fan_alarm_store,
		FANCPLD_ALARM_NODE, 0, 0,
	},
	{
		"fan7_min",
		NULL,
		fan_alarm_show,
		fan_alarm_store,
		FANCPLD_ALARM_NODE, 0, 0,
	},
	{
		"fan7_max",
		NULL,
		fan_alarm_show,
		fan_alarm_store,
		FANCPLD_ALARM_NODE, 0, 0,
	},
	{
		"fan8_min",
		NULL,
		fan_alarm_show,
		fan_alarm_store,
		FANCPLD_ALARM_NODE, 0, 0,
	},
	{
		"fan8_max",
		NULL,
		fan_alarm_show,
		fan_alarm_store,
		FANCPLD_ALARM_NODE, 0, 0,
	},
	{
		"fan9_min",
		NULL,
		fan_alarm_show,
		fan_alarm_store,
		FANCPLD_ALARM_NODE, 0, 0,
	},
	{
		"fan9_max",
		NULL,
		fan_alarm_show,
		fan_alarm_store,
		FANCPLD_ALARM_NODE, 0, 0,
	},
	{
		"fan10_min",
		NULL,
		fan_alarm_show,
		fan_alarm_store,
		FANCPLD_ALARM_NODE, 0, 0,
	},
	{
		"fan10_max",
		NULL,
		fan_alarm_show,
		fan_alarm_store,
		FANCPLD_ALARM_NODE, 0, 0,
	},

};

static int fancpld_remove(struct i2c_client *client)
{
	i2c_dev_sysfs_data_clean(client, &fancpld_data);
	return 0;
}

static int fancpld_probe(struct i2c_client *client,
                         const struct i2c_device_id *id)
{
	int n_attrs;

	if (!i2c_check_functionality(client->adapter,
	                             I2C_FUNC_SMBUS_READ_BYTE | I2C_FUNC_SMBUS_READ_BYTE_DATA))
		return -ENODEV;

	n_attrs = sizeof(fancpld_attr_table) / sizeof(fancpld_attr_table[0]);

	return i2c_dev_sysfs_data_init(client, &fancpld_data, fancpld_attr_table, n_attrs);
}


static struct i2c_driver fancpld_driver = {
	.driver = {
		.name = "fancpld",
	},
	.probe = fancpld_probe,
	.remove = fancpld_remove,
	.id_table = fancpld_id,
};

module_i2c_driver(fancpld_driver);

MODULE_AUTHOR("Micky Zhan@Celestica.com");
MODULE_DESCRIPTION("Fan CPLD driver");
MODULE_VERSION("0.0.1");
MODULE_LICENSE("GPL");

