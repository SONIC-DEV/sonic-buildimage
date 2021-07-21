/*
 * SYS CPLD driver for CPLD and compatibles
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

// #define DEBUG
#define SYSCPLD_ALARM_NODE 0xff /*just for flag using*/
#define SYSFS_READ 0
#define SYSFS_WRITE 1

#ifdef DEBUG
#define SYSCPLD_DEBUG(fmt, ...) do {              \
  printk(KERN_DEBUG "%s:%d " fmt "\n",            \
         __FUNCTION__, __LINE__, ##__VA_ARGS__);  \
} while (0)

#else /* !DEBUG */

#define SYSCPLD_DEBUG(fmt, ...)
#endif


enum chips {
	SYSCPLD = 1,
};

struct temp_data_t {
	int input;
	int max;
	int max_hyst;
};

struct temp_data {
	struct temp_data_t temp1;
};

struct temp_data switch_temp_data;
struct temp_data cpu_temp_data;
struct i2c_client *syscpld_client;

static const struct i2c_device_id syscpld_id[] = {
	{"syscpld", SYSCPLD },
	{ }
};

static int board_type;

static int temp_value_rw(const char *name, int opcode, int value)
{
	int *p = NULL;

	if (strcmp(name, "temp1_max") == 0) {
		p = &switch_temp_data.temp1.max;
	} else if (strcmp(name, "temp1_max_hyst") == 0) {
		p = &switch_temp_data.temp1.max_hyst;
	} else if (strcmp(name, "temp2_input") == 0) {
		p = &cpu_temp_data.temp1.input;
	} else if (strcmp(name, "temp2_max") == 0) {
		p = &cpu_temp_data.temp1.max;
	} else if (strcmp(name, "temp2_max_hyst") == 0) {
		p = &cpu_temp_data.temp1.max_hyst;
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

static ssize_t switch_temp_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
	int temp_val = 0;
	int freq = i2c_dev_read_word_bigendian(dev, attr);

	if (freq <= 0)
	{
		freq = 1;
		SYSCPLD_DEBUG("Read Swich chip temperature error!\n");
	}

	temp_val = 434100 - (12500000 / freq - 1) * 535;
	if (temp_val > 200000 || temp_val < 0) temp_val = 0;

	return scnprintf(buf, PAGE_SIZE, "%d\n", temp_val);
}

static ssize_t sys_alarm_show(struct device *dev,
                              struct device_attribute *attr,
                              char *buf)
{
	int value = -1;
	i2c_sysfs_attr_st *i2c_attr = TO_I2C_SYSFS_ATTR(attr);
	const i2c_dev_attr_st *dev_attr = i2c_attr->isa_i2c_attr;
	const char *name = dev_attr->ida_name;

	if (!name)
		return -1;

	value = temp_value_rw(name, SYSFS_READ, 0);

	return scnprintf(buf, PAGE_SIZE, "%d\n", value);
}

static ssize_t sys_alarm_store(struct device *dev,
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
	rc = temp_value_rw(name, SYSFS_WRITE, write_value);
	if (rc < 0)
		return -1;

	return count;
}

static i2c_dev_data_st syscpld_data;
static const i2c_dev_attr_st syscpld_attr_table_b3010[] = {
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
		0x1, 0, 8,
	},
	{
		"hardware_version",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x2, 0, 2,
	},
	{
		"sw_brd_type",
		"Indicate the board type\n"
		"0x00: fishbone32\n"
		"0x01: b3010",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x3, 0, 2,
	},
	{
		"sb_reset",
		"switch board reset control:\n"
		"0x0: reset\n"
		"0x1 not reset",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x4, 0, 1,
	},
	{
		"i210_reset",
		"I210 reset control:\n"
		"0x0: reset\n"
		"0x1 not reset",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x4, 1, 1,
	},
	{
		"pca9548_reset",
		"PCA9548 reset control:\n"
		"0x0: reset\n"
		"0x1 not reset",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x4, 2, 1,
	},
	{
		"fan_cpld_reset",
		"FAN CPLD reset control:\n"
		"0x0: reset\n"
		"0x1 not reset",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x4, 3, 1,
	},
	{
		"bmc_reset",
		"BMC reset control:\n"
		"0x0: reset\n"
		"0x1 not reset",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x4, 4, 1,
	},
	{
		"bcm5387_reset",
		"BCM5387 reset control:\n"
		"0x0: reset\n"
		"0x1 not reset",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x4, 5, 1,
	},
	{
		"tpm_reset",
		"TPM reset control:\n"
		"0x0: reset\n"
		"0x1 not reset",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x4, 6, 1,
	},
	{
		"come_rst_st",
		"0x11: power on reset\n"
		"0x22: software trigger CPU to warm reset\n"
		"0x33: software trigger CPU to cold reset\n"
		"0x44: CPU warm reset\n"
		"0x55: CPU cold reset\n"
		"0x66: watchdog reset\n"
		"0x77: power cycle",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x6, 0, 8,
	},
	{
		"usb_iso_en",
		"0x0: enable\n"
		"0x1: disable",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0xa, 0, 1,
	},
	{
		"usb_front_oc",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0xb, 0, 1,
	},
	{
		"tps2051_oc",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0xb, 1, 1,
	},
	{
		"sol_control",
		"0x0: switch to BMC\n"
		"0x1: switch to COMe",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0xc, 0, 1,
	},
	{
		"cpu_int_status",
		"0x0: interrupt\n"
		"0x1: no interrupt",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x10, 0, 1,
	},
	{
		"bmc_int_status",
		"0x0: interrupt\n"
		"0x1: no interrupt",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x10, 1, 1,
	},
	{
		"sw_lm75_int_status",
		"0x0: interrupt\n"
		"0x1: no interrupt",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x11, 0, 1,
	},
	{
		"i210_wake_int_status",
		"0x0: interrupt\n"
		"0x1: no interrupt",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x11, 1, 1,
	},
	{
		"psu1_int",
		"0x0: interrupt\n"
		"0x1: no interrupt",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x11, 2, 1,
	},
	{
		"psu2_int",
		"0x0: interrupt\n"
		"0x1: no interrupt",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x11, 3, 1,
	},
	{
		"bmc_54616s_int_status",
		"0x0: interrupt\n"
		"0x1: no interrupt",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x11, 4, 1,
	},
	{
		"gbe_54616s_int_status",
		"0x0: interrupt\n"
		"0x1: no interrupt",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x11, 5, 1,
	},
	{
		"gbe_54616s_b_int_status",
		"0x0: interrupt\n"
		"0x1: no interrupt",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x11, 6, 1,
	},
	{
		"pwr_come_en",
		"0x0: COMe power is off\n"
		"0x1: COMe power is on",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x20, 0, 1,
	},
	{
		"come_rst_n",
		"0x0: trigger COMe reset\n"
		"0x1: normal",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x21, 0, 1,
	},
	{
		"come_status",
		"0x1: SUS_S3_N\n"
		"0x2: SUS_S4_N\n"
		"0x4: SUS_S5_N\n"
		"0x8: SUS_STAT_N",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x22, 0, 4,
	},
	{
		"bios_cs",
		"0x1: select BIOS0\n"
		"0x3: select BIOS1",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x23, 0, 2,
	},
	{
		"bios_ctrl",
		"0x0: connect to CPU, control by software\n"
		"0x1: disable connect to CPU, control by CPLD\n"
		"0x2: connect to BMC, control by software",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x23, 4, 2,
	},
	{
		"cb_pwr_btn_n",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x24, 0, 1,
	},
	{
		"cb_type_n",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x25, 0, 3,
	},
	{
		"switch_power_f",
		"0x1: force to switch card power off\n"
		"0x2: force to switch card power on",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x26, 0, 2,
	},
	{
		"cb_rst_n",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x27, 0, 1,
	},
	{
		"bios_spi_wp0_n",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x31, 0, 1,
	},
	{
		"bios_spi_wp1_n",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x31, 1, 1,
	},
	{
		"tlv_eeprom_wp",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x31, 2, 1,
	},
	{
		"sys_eeprom_wp",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x31, 3, 1,
	},
	{
		"fru_eeprom_wp",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x31, 4, 1,
	},
	{
		"psu_r_en",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x5f, 0, 1,
	},
	{
		"psu_l_en",
		NULL,
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x5f, 1, 1,
	},
	{
		"psu_r_status",
		"0x0: not OK\n"
		"0x1: OK",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x60, 0, 1,
	},
	{
		"psu_l_status",
		"0x0: not OK\n"
		"0x1: OK",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x60, 1, 1,
	},
	{
		"psu_r_present",
		"0x0: present\n"
		"0x1: absent",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x60, 2, 1,
	},
	{
		"psu_l_present",
		"0x0: present\n"
		"0x1: absent",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x60, 3, 1,
	},
	{
		"psu_r_int_status",
		"0x0: interrupt\n"
		"0x1: no interrupt",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x60, 4, 1,
	},
	{
		"psu_l_int_status",
		"0x0: interrupt\n"
		"0x1: no interrupt",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x60, 5, 1,
	},
	{
		"psu_r_ac_status",
		"0x0: not OK\n"
		"0x1: OK",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x60, 6, 1,
	},
	{
		"psu_l_ac_status",
		"0x0: not OK\n"
		"0x1: OK",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x60, 7, 1,
	},
	{
		"psu_l_led_ctrl_en",
		"0x0: disable\n"
		"0x1: enable",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x61, 0, 1,
	},
	{
		"psu_r_led_ctrl_en",
		"0x0: disable\n"
		"0x1: enable",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x61, 1, 1,
	},
	{
		"sysled_ctrl",
		"0x0: on\n"
		"0x1: 1HZ blink\n"
		"0x2: 4HZ blink\n"
		"0x3: off",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x62, 0, 2,
	},
	{
		"sysled_select",
		"0x0:  green and yellow alternate blink\n"
		"0x1: green\n"
		"0x2: yellow\n"
		"0x3: off",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x62, 4, 2,
	},
	{
		"fan_led_ctrl_en",
		"0x0: disable\n"
		"0x1: enable\n",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x63, 0, 1,
	},
	{
		"pwr_cycle",
		"0x0: enable\n"
		"0x1: disable",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x64, 0, 1,
	},
	{
		"bios_boot_ok",
		"0x0: not ok\n"
		"0x1: ok",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x70, 0, 1,
	},
	{
		"bios_boot_cs",
		"0x0: from BIOS0\n"
		"0x1: from BIOS1",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		NULL,
		0x70, 1, 1,
	},
	{
		"boot_counter",
		NULL,
		NULL,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x71, 0, 8,
	},
	{
		"thermal_shutdown_en",
		"0x0: disable\n"
		"0x1: enable",
		I2C_DEV_ATTR_SHOW_DEFAULT,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x75, 0, 1,
	},
	{
		"temp1_input",
		"Switch chip Temperature",
		switch_temp_show,
		I2C_DEV_ATTR_STORE_DEFAULT,
		0x7A, 0, 8,
	},
	{
		"temp1_max",
		"Switch chip Temperature",
		sys_alarm_show,
		sys_alarm_store,
		SYSCPLD_ALARM_NODE, 0, 8,
	},
	{
		"temp1_max_hyst",
		"Switch chip Temperature",
		sys_alarm_show,
		sys_alarm_store,
		SYSCPLD_ALARM_NODE, 0, 8,
	},
	{
		"temp2_input",
		"CPU chip Temperature",
		sys_alarm_show,
		sys_alarm_store,
		SYSCPLD_ALARM_NODE, 0, 8,
	},
	{
		"temp2_max",
		"CPU chip Temperature",
		sys_alarm_show,
		sys_alarm_store,
		SYSCPLD_ALARM_NODE, 0, 8,
	},
	{
		"temp2_max_hyst",
		"CPU chip Temperature",
		sys_alarm_show,
		sys_alarm_store,
		SYSCPLD_ALARM_NODE, 0, 8,
	},
};

static int syscpld_remove(struct i2c_client *client)
{
	syscpld_client = NULL;
	i2c_dev_sysfs_data_clean(client, &syscpld_data);
	return 0;
}

static int syscpld_probe(struct i2c_client *client,
                         const struct i2c_device_id *id)
{
	int n_attrs;
	i2c_dev_attr_st *syscpld_attr_table;

	if (!i2c_check_functionality(client->adapter,
	                             I2C_FUNC_SMBUS_READ_BYTE | I2C_FUNC_SMBUS_READ_BYTE_DATA))
		return -ENODEV;

	syscpld_client = client;
	//check board type then add different file nodes
	board_type = i2c_smbus_read_byte_data(syscpld_client, 0x2);

	if (board_type < 0) {
		dev_err(&client->dev, "Cannot read board type.\n");
		return board_type;
	} else {
		printk(KERN_INFO "Questone2bd CPLD driver loading\n");
		n_attrs = sizeof(syscpld_attr_table_b3010) / sizeof(syscpld_attr_table_b3010[0]);
		syscpld_attr_table = syscpld_attr_table_b3010;
	}

	return i2c_dev_sysfs_data_init(client, &syscpld_data, syscpld_attr_table, n_attrs);
}

/*
 * 0-present		1-power ok
 * PSU1: present:bit2	power:bit0
 * PSU2: present:bit3	power:bit1
 * PSU1: 0x59		PSU2: 0x58
 * result: 0 is not OK, 1 is OK
 */
int psu_ok(int bus, unsigned short addr)
{
	int ret = 0;
	i2c_dev_data_st *data;
	int psu_status;

	if (syscpld_client == NULL)
		return -ENODEV;

	data = i2c_get_clientdata(syscpld_client);
	mutex_lock(&data->idd_lock);

	psu_status = i2c_smbus_read_byte_data(syscpld_client, 0x60);
	if (addr == 0x59) {
		if ((psu_status & 0x5) == 0x1)
			ret = 1;
	} else if (addr == 0x58) {
		if ((psu_status & 0xa) == 0x2)
			ret = 1;
	} else {
		ret = 0;
	}

	mutex_unlock(&data->idd_lock);
	return ret;
}
EXPORT_SYMBOL(psu_ok);

static struct i2c_driver syscpld_driver = {
	.driver = {
		.name = "syscpld",
	},
	.probe = syscpld_probe,
	.remove = syscpld_remove,
	.id_table = syscpld_id,
};

module_i2c_driver(syscpld_driver);

MODULE_AUTHOR("Micky Zhan@Celestica.com");
MODULE_DESCRIPTION("system CPLD driver for CPLD");
MODULE_VERSION("0.0.1");
MODULE_LICENSE("GPL");

