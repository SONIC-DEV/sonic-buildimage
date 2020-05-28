/*
 * cls-switchboard.c - PCI device driver for Silverstone2 Switch board FPGA.
 *
 * Author: Pradchaya Phucharoen
 *
 * Copyright (C) 2019 Celestica Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/acpi.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/platform_data/pca954x.h>
#include <linux/platform_data/i2c-ocores.h>
#include "xcvr-cls.h"

#define MOD_VERSION "1.0.1"
#define DRV_NAME "cls-switchboard"

#define I2C_MUX_CHANNEL(_ch, _adap_id, _deselect) \
	[_ch] = { .adap_id = _adap_id, .deselect_on_exit = _deselect }

#define FPGA_PCIE_DEVICE_ID	0x7021
#define MMIO_BAR		0
/* Reserve some bus numbers for CPU or FPGA */
#define I2C_BUS_OFS		14

/* I2C ocore configurations */
#define OCORE_REGSHIFT      	      2
#define OCORE_IP_CLK_khz    	      62500
#define OCORE_BUS_CLK_khz   	      100
#define OCORE_REG_IO_WIDTH  	      1

/* Optical port xcvr configuration */
#define XCVR_REG_SHIFT		2
#define XCVR_NUM_PORT		34
#define XCVR_PORT_REG_SIZE	0x10

/* i2c_bus_config - an i2c-core resource and platform data
 *  @id - I2C bus device ID, for identification.
 *  @res - resources for an i2c-core device.
 *  @num_res - size of the resources.
 *  @pdata - a platform data of an i2c-core device.
 */
struct i2c_bus_config {
	int id;
	struct resource *res;
	ssize_t num_res;
	struct ocores_i2c_platform_data pdata;
};

/* switchbrd_priv - switchboard private data */
struct switchbrd_priv {
	unsigned long base;
	int num_i2c_bus;
	struct platform_device **i2cbuses_pdev;
	struct platform_device *regio_pdev;
	struct platform_device *spiflash_pdev;
	struct platform_device *xcvr_pdev;
};

/* I2C bus speed param */
static int bus_clock_master_1 = 100;
module_param(bus_clock_master_1, int, 0660);
MODULE_PARM_DESC(bus_clock_master_1,
	"I2C master 1 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_2 = 100;
module_param(bus_clock_master_2, int, 0660);
MODULE_PARM_DESC(bus_clock_master_2,
	"I2C master 2 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_3 = 100;
module_param(bus_clock_master_3, int, 0660);
MODULE_PARM_DESC(bus_clock_master_3,
	"I2C master 3 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_4 = 100;
module_param(bus_clock_master_4, int, 0660);
MODULE_PARM_DESC(bus_clock_master_4,
	"I2C master 4 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_5 = 100;
module_param(bus_clock_master_5, int, 0660);
MODULE_PARM_DESC(bus_clock_master_5,
	"I2C master 5 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_6 = 100;
module_param(bus_clock_master_6, int, 0660);
MODULE_PARM_DESC(bus_clock_master_6,
	"I2C master 6 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_7 = 100;
module_param(bus_clock_master_7, int, 0660);
MODULE_PARM_DESC(bus_clock_master_7,
	"I2C master 7 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_8 = 100;
module_param(bus_clock_master_8, int, 0660);
MODULE_PARM_DESC(bus_clock_master_8,
	"I2C master 8 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_9 = 100;
module_param(bus_clock_master_9, int, 0660);
MODULE_PARM_DESC(bus_clock_master_9,
	"I2C master 9 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_10 = 100;
module_param(bus_clock_master_10, int, 0660);
MODULE_PARM_DESC(bus_clock_master_10,
	"I2C master 10 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_11 = 100;
module_param(bus_clock_master_11, int, 0660);
MODULE_PARM_DESC(bus_clock_master_11,
	"I2C master 11 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_12 = 100;
module_param(bus_clock_master_12, int, 0660);
MODULE_PARM_DESC(bus_clock_master_12,
	"I2C master 12 bus speed in KHz 50/80/100/200/400");

static int bus_clock_master_13 = 100;
module_param(bus_clock_master_13, int, 0660);
MODULE_PARM_DESC(bus_clock_master_13,
	"I2C master 13 bus speed in KHz 50/80/100/200/400");

/* PCA9548 channel config on MASTER BUS 3 and MASTER BUS 6 */
static struct pca954x_platform_mode i2c_mux_70_modes[] = {
	I2C_MUX_CHANNEL(0, I2C_BUS_OFS + 27, true),
	I2C_MUX_CHANNEL(1, I2C_BUS_OFS + 32, true),
	I2C_MUX_CHANNEL(2, I2C_BUS_OFS + 29, true),
	I2C_MUX_CHANNEL(3, I2C_BUS_OFS + 31, true),
	I2C_MUX_CHANNEL(4, I2C_BUS_OFS + 30, true),
	I2C_MUX_CHANNEL(5, I2C_BUS_OFS + 23, true),
	I2C_MUX_CHANNEL(6, I2C_BUS_OFS + 26, true),
	I2C_MUX_CHANNEL(7, I2C_BUS_OFS + 28, true),
};

static struct pca954x_platform_mode i2c_mux_71_modes[] = {
	I2C_MUX_CHANNEL(0, I2C_BUS_OFS + 3, true),
	I2C_MUX_CHANNEL(1, I2C_BUS_OFS + 4, true),
	I2C_MUX_CHANNEL(2, I2C_BUS_OFS + 1, true),
	I2C_MUX_CHANNEL(3, I2C_BUS_OFS + 2, true),
	I2C_MUX_CHANNEL(4, I2C_BUS_OFS + 8, true),
	I2C_MUX_CHANNEL(5, I2C_BUS_OFS + 6, true),
	I2C_MUX_CHANNEL(6, I2C_BUS_OFS + 5, true),
	I2C_MUX_CHANNEL(7, I2C_BUS_OFS + 15, true),
};

static struct pca954x_platform_mode i2c_mux_72_modes[] = {
	I2C_MUX_CHANNEL(0, I2C_BUS_OFS + 20, true),
	I2C_MUX_CHANNEL(1, I2C_BUS_OFS + 17, true),
	I2C_MUX_CHANNEL(2, I2C_BUS_OFS + 22, true),
	I2C_MUX_CHANNEL(3, I2C_BUS_OFS + 25, true),
	I2C_MUX_CHANNEL(4, I2C_BUS_OFS + 19, true),
	I2C_MUX_CHANNEL(5, I2C_BUS_OFS + 21, true),
	I2C_MUX_CHANNEL(6, I2C_BUS_OFS + 24, true),
	I2C_MUX_CHANNEL(7, I2C_BUS_OFS + 18, true),
};

static struct pca954x_platform_mode i2c_mux_73_modes[] = {
	I2C_MUX_CHANNEL(0, I2C_BUS_OFS + 16, true),
	I2C_MUX_CHANNEL(1, I2C_BUS_OFS + 13, true),
	I2C_MUX_CHANNEL(2, I2C_BUS_OFS + 12, true),
	I2C_MUX_CHANNEL(3, I2C_BUS_OFS + 10, true),
	I2C_MUX_CHANNEL(4, I2C_BUS_OFS + 9, true),
	I2C_MUX_CHANNEL(5, I2C_BUS_OFS + 14, true),
	I2C_MUX_CHANNEL(6, I2C_BUS_OFS + 11, true),
	I2C_MUX_CHANNEL(7, I2C_BUS_OFS + 7, true),
};

static struct pca954x_platform_data om_muxes[] = {
	{
		.modes = i2c_mux_70_modes,
		.num_modes = ARRAY_SIZE(i2c_mux_70_modes),
	},
	{
		.modes = i2c_mux_71_modes,
		.num_modes = ARRAY_SIZE(i2c_mux_71_modes),
	},
	{
		.modes = i2c_mux_72_modes,
		.num_modes = ARRAY_SIZE(i2c_mux_72_modes),
	},
	{
		.modes = i2c_mux_73_modes,
		.num_modes = ARRAY_SIZE(i2c_mux_73_modes),
	},
};

/* Optical Module bus 3 i2c muxes info */
static struct i2c_board_info i2c_info_3[] = {
	{
		I2C_BOARD_INFO("pca9548", 0x71),
		.platform_data = &om_muxes[1],
	},
	{
		I2C_BOARD_INFO("pca9548", 0x73),
		.platform_data = &om_muxes[3],
	},
};

/* Optical Module bus 6 i2c muxes info */
static struct i2c_board_info i2c_info_6[] = {
	{
		I2C_BOARD_INFO("pca9548", 0x70),
		.platform_data = &om_muxes[0],
	},
	{
		I2C_BOARD_INFO("pca9548", 0x72),
		.platform_data = &om_muxes[2],
	},
};

/* RESOURCE SEPERATES BY FUNCTION */
/* Resource IOMEM for i2c bus 1 for SFP1 */
static struct resource  cls_i2c_res_1[] = {
	{
		.start = 0x800, .end = 0x81F,
		.flags = IORESOURCE_MEM,},
};

/* Resource IOMEM for i2c bus 2 for SFP2 */
static struct resource  cls_i2c_res_2[] = {
	{
		.start = 0x820, .end = 0x83F,
		.flags = IORESOURCE_MEM,},
};

/* Resource IOMEM for i2c bus 3 for QSFPDD */
static struct resource  cls_i2c_res_3[] = {
	{
		.start = 0x840, .end = 0x85F,
		.flags = IORESOURCE_MEM,},
};

/* Resource IOMEM for i2c bus 4 for switchboard CPPLD1/CPLD2 */
static struct resource  cls_i2c_res_4[] = {
	{
		.start = 0x860, .end = 0x87F,
		.flags = IORESOURCE_MEM,},
};

/* Resource IOMEM for i2c bus 5 */
static struct resource  cls_i2c_res_5[] = {
	{
		.start = 0x880, .end = 0x89F,
		.flags = IORESOURCE_MEM,},
};

/* Resource IOMEM for i2c bus 6 for QSFPDD */
static struct resource  cls_i2c_res_6[] = {
	{
		.start = 0x8A0, .end = 0x8BF,
		.flags = IORESOURCE_MEM,},
};

static struct resource  cls_i2c_res_7[] = {
	{
		.start = 0x8C0, .end = 0x8DF,
		.flags = IORESOURCE_MEM,},
};

static struct resource  cls_i2c_res_8[] = {
	{
		.start = 0x8E0, .end = 0x8FF,
		.flags = IORESOURCE_MEM,},
};

static struct resource  cls_i2c_res_9[] = {
	{
		.start = 0x900, .end = 0x91F,
		.flags = IORESOURCE_MEM,},
};

static struct resource  cls_i2c_res_10[] = {
	{
		.start = 0x920, .end = 0x93F,
		.flags = IORESOURCE_MEM,},
};

/* Resource IOMEM for i2c bus 11 for si5344D */
static struct resource  cls_i2c_res_11[] = {
	{
		.start = 0x940, .end = 0x95F,
		.flags = IORESOURCE_MEM,},
};

static struct resource  cls_i2c_res_12[] = {
	{
		.start = 0x960, .end = 0x97F,
		.flags = IORESOURCE_MEM,},
};

/* Resource IOMEM for i2c bus 13 for VDDCORE_12V_DCDC */
static struct resource  cls_i2c_res_13[] = {
	{
		.start = 0x980, .end = 0x99F,
		.flags = IORESOURCE_MEM,},
};

/* Resource IOMEM for reg access */
static struct resource reg_io_res[] = {
	{
		.start = 0x00, .end = 0xFF,
		.flags = IORESOURCE_MEM,},
};

/* Resource IOMEM for spi flash firmware upgrade */
static struct resource spi_flash_res[] = {
	{
		.start = 0x1200, .end = 0x121F,
		.flags = IORESOURCE_MEM,},
};

/* Resource IOMEM for front panel XCVR */
static struct resource xcvr_res[] = {
	{
		.start = 0x4000, .end = 0x421F,
		.flags = IORESOURCE_MEM,},
};


/*
 * Notes: Some FPGA_I2C_Master buses are shared with BMC, these buses need to
 *        stay uninitialized because they interfere the BMC activity.
 */
static struct i2c_bus_config i2c_bus_configs[] = {
	{
		.id = 1,
		.res = cls_i2c_res_1,
		.num_res = ARRAY_SIZE(cls_i2c_res_1),
		.pdata = {
			.reg_shift = OCORE_REGSHIFT,
			.reg_io_width = OCORE_REG_IO_WIDTH,
			.clock_khz = OCORE_IP_CLK_khz,
			.bus_khz = OCORE_BUS_CLK_khz,
			.big_endian = false,
			.num_devices = 0,
			.devices = NULL,
		},
	},
	{
		.id = 2,
		.res = cls_i2c_res_2,
		.num_res = ARRAY_SIZE(cls_i2c_res_2),
		.pdata = {
			.reg_shift = OCORE_REGSHIFT,
			.reg_io_width = OCORE_REG_IO_WIDTH,
			.clock_khz = OCORE_IP_CLK_khz,
			.bus_khz = OCORE_BUS_CLK_khz,
			.big_endian = false,
			.num_devices = 0,
			.devices = NULL,
		},
	},
	{
		.id = 3,
		.res = cls_i2c_res_3,
		.num_res = ARRAY_SIZE(cls_i2c_res_3),
		.pdata = {
			.reg_shift = OCORE_REGSHIFT,
			.reg_io_width = OCORE_REG_IO_WIDTH,
			.clock_khz = OCORE_IP_CLK_khz,
			.bus_khz = OCORE_BUS_CLK_khz,
			.big_endian = false,
			.num_devices = ARRAY_SIZE(i2c_info_3),
			.devices = i2c_info_3,
		},
	},
	{
		.id = 4,
		.res = cls_i2c_res_4,
		.num_res = ARRAY_SIZE(cls_i2c_res_4),
		.pdata = {
			.reg_shift = OCORE_REGSHIFT,
			.reg_io_width = OCORE_REG_IO_WIDTH,
			.clock_khz = OCORE_IP_CLK_khz,
			.bus_khz = OCORE_BUS_CLK_khz,
			.big_endian = false,
			.num_devices = 0,
			.devices = NULL,
		},
	},
	{
		.id = 6,
		.res = cls_i2c_res_6,
		.num_res = ARRAY_SIZE(cls_i2c_res_6),
		.pdata = {
			.reg_shift = OCORE_REGSHIFT,
			.reg_io_width = OCORE_REG_IO_WIDTH,
			.clock_khz = OCORE_IP_CLK_khz,
			.bus_khz = OCORE_BUS_CLK_khz,
			.big_endian = false,
			.num_devices = ARRAY_SIZE(i2c_info_6),
			.devices = i2c_info_6,
		},
	},
	{
		.id = 11,
		.res = cls_i2c_res_11,
		.num_res = ARRAY_SIZE(cls_i2c_res_11),
		.pdata = {
			.reg_shift = OCORE_REGSHIFT,
			.reg_io_width = OCORE_REG_IO_WIDTH,
			.clock_khz = OCORE_IP_CLK_khz,
			.bus_khz = OCORE_BUS_CLK_khz,
			.big_endian = false,
			.num_devices = 0,
			.devices = NULL,
		},
	},
};

/* xcvr front panel port mapping */
static struct port_info front_panel_ports[] = {
	{"QSFPDD1",   1, QSFP},
	{"QSFPDD2",   2, QSFP},
	{"QSFPDD3",   3, QSFP},
	{"QSFPDD4",   4, QSFP},
	{"QSFPDD5",   5, QSFP},
	{"QSFPDD6",   6, QSFP},
	{"QSFPDD7",   7, QSFP},
	{"QSFPDD8",   8, QSFP},
	{"QSFPDD9",   9, QSFP},
	{"QSFPDD10", 10, QSFP},
	{"QSFPDD11", 11, QSFP},
	{"QSFPDD12", 12, QSFP},
	{"QSFPDD13", 13, QSFP},
	{"QSFPDD14", 14, QSFP},
	{"QSFPDD15", 15, QSFP},
	{"QSFPDD16", 16, QSFP},
	{"QSFPDD17", 17, QSFP},
	{"QSFPDD18", 18, QSFP},
	{"QSFPDD19", 19, QSFP},
	{"QSFPDD20", 20, QSFP},
	{"QSFPDD21", 21, QSFP},
	{"QSFPDD22", 22, QSFP},
	{"QSFPDD23", 23, QSFP},
	{"QSFPDD24", 24, QSFP},
	{"QSFPDD25", 25, QSFP},
	{"QSFPDD26", 26, QSFP},
	{"QSFPDD27", 27, QSFP},
	{"QSFPDD28", 28, QSFP},
	{"QSFPDD29", 29, QSFP},
	{"QSFPDD30", 30, QSFP},
	{"QSFPDD31", 31, QSFP},
	{"QSFPDD32", 32, QSFP},
	{"SFP+1",    33, SFP},
	{"SFP+2",    34, SFP},
	/* END OF LIST */
};

static struct cls_xcvr_platform_data xcvr_data = {
	.port_reg_size = 0x10,
	.num_ports = ARRAY_SIZE(front_panel_ports),
	.devices = front_panel_ports,
};


// TODO: Add a platform configuration struct, and use probe as a factory,
//	 so xcvr, fwupgrade device can configured as options.

static int cls_fpga_probe(struct pci_dev *dev, const struct pci_device_id *id)
{

	struct switchbrd_priv *priv;
	struct platform_device **i2cbuses_pdev;
	struct platform_device *regio_pdev;
	struct platform_device *xcvr_pdev;
	unsigned long rstart;
	int num_i2c_bus, i;
	int err;

	err = pci_enable_device(dev);
	if (err){
		dev_err(&dev->dev,  "Failed to enable PCI device\n");
		goto err_exit;
	}

	/* Check for valid MMIO address */
	rstart = pci_resource_start(dev, MMIO_BAR);
	if (!rstart) {
		dev_err(&dev->dev, "Switchboard base address uninitialized, "
			"check FPGA\n");
		err = -ENODEV;
		goto err_disable_device;
	}

	dev_dbg(&dev->dev, "BAR%d res: 0x%lx-0x%llx\n", MMIO_BAR,
		rstart, pci_resource_end(dev, MMIO_BAR));

	priv = devm_kzalloc(&dev->dev,
				sizeof(struct switchbrd_priv), GFP_KERNEL);
	if (!priv){
		err = -ENOMEM;
		goto err_disable_device;
	}

	pci_set_drvdata(dev, priv);
	num_i2c_bus = ARRAY_SIZE(i2c_bus_configs);
	i2cbuses_pdev = devm_kzalloc(
				&dev->dev,
				num_i2c_bus * sizeof(struct platform_device*),
				GFP_KERNEL);

	reg_io_res[0].start += rstart;
	reg_io_res[0].end += rstart;

	xcvr_res[0].start += rstart;
	xcvr_res[0].end += rstart;

	regio_pdev = platform_device_register_resndata(
			&dev->dev, "cls-swbrd-io",
			-1,
			reg_io_res, ARRAY_SIZE(reg_io_res),
			NULL, 0);

	if (IS_ERR(regio_pdev)) {
		dev_err(&dev->dev, "Failed to register cls-swbrd-io\n");
		err = PTR_ERR(regio_pdev);
		goto err_disable_device;
	}

	xcvr_pdev = platform_device_register_resndata(
						      NULL,
						      "cls-xcvr",
						      -1,
						      xcvr_res,
						      ARRAY_SIZE(xcvr_res),
						      &xcvr_data,
						      sizeof(xcvr_data));

	if (IS_ERR(xcvr_pdev)) {
		dev_err(&dev->dev, "Failed to register xcvr node\n");
		err = PTR_ERR(xcvr_pdev);
		goto err_unregister_regio;
	}

	for(i = 0; i < num_i2c_bus; i++){

		i2c_bus_configs[i].res[0].start += rstart;
		i2c_bus_configs[i].res[0].end += rstart;

		printk("start %x ... end %x\n",i2c_bus_configs[i].res[0].start,i2c_bus_configs[i].res[0].end);

		switch (i + 1) {
		case 1:
			i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_1;
			break;
		case 2:
			i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_2;
			break;
		case 3:
			i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_3;
			break;
		case 4:
			i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_4;
			break;
		case 5:
			i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_5;
			break;
		case 6:
			i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_6;
			break;
		case 7:
			i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_7;
			break;
		case 8:
			i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_8;
			break;
		case 9:
			i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_9;
			break;
		case 10:
			i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_10;
			break;
		case 11:
			i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_11;
			break;
		case 12:
			i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_12;
			break;
		case 13:
			i2c_bus_configs[i].pdata.bus_khz = bus_clock_master_13;
			break;
		default:
			i2c_bus_configs[i].pdata.bus_khz = OCORE_BUS_CLK_khz;
		}

		dev_dbg(&dev->dev, "i2c-bus.%d: 0x%llx - 0x%llx\n",
			i2c_bus_configs[i].id,
			i2c_bus_configs[i].res[0].start,
			i2c_bus_configs[i].res[0].end);

		i2cbuses_pdev[i] = platform_device_register_resndata(
					&dev->dev, "ocores-i2c",
					i2c_bus_configs[i].id,
					i2c_bus_configs[i].res,
					i2c_bus_configs[i].num_res,
					&i2c_bus_configs[i].pdata,
					sizeof(i2c_bus_configs[i].pdata));

		if (IS_ERR(i2cbuses_pdev[i])) {
			dev_err(&dev->dev, "Failed to register ocores-i2c.%d\n",
				i2c_bus_configs[i].id);
			err = PTR_ERR(i2cbuses_pdev[i]);
			goto err_unregister_ocore;
		}
	}

	priv->base = rstart;
	priv->num_i2c_bus = num_i2c_bus;
	priv->i2cbuses_pdev = i2cbuses_pdev;
	priv->regio_pdev = regio_pdev;
	priv->xcvr_pdev = xcvr_pdev;
	return 0;

err_unregister_ocore:
	for(i = 0; i < num_i2c_bus; i++){
		if(priv->i2cbuses_pdev[i]){
			platform_device_unregister(priv->i2cbuses_pdev[i]);
		}
	}
err_unregister_xcvr:
	platform_device_unregister(xcvr_pdev);
err_unregister_regio:
	platform_device_unregister(regio_pdev);
err_disable_device:
	pci_disable_device(dev);
err_exit:
	return err;
}

static void cls_fpga_remove(struct pci_dev *dev)
{
	int i;
	struct switchbrd_priv *priv = pci_get_drvdata(dev);

	for(i = 0; i < priv->num_i2c_bus; i++){
		if(priv->i2cbuses_pdev[i])
			platform_device_unregister(priv->i2cbuses_pdev[i]);
	}
	platform_device_unregister(priv->xcvr_pdev);
	platform_device_unregister(priv->regio_pdev);
	pci_disable_device(dev);
	return;
};

static const struct pci_device_id pci_clsswbrd[] = {
	{  PCI_VDEVICE(XILINX, FPGA_PCIE_DEVICE_ID) },
	{0, }
};

MODULE_DEVICE_TABLE(pci, pci_clsswbrd);

static struct pci_driver clsswbrd_pci_driver = {
	.name = DRV_NAME,
	.id_table = pci_clsswbrd,
	.probe = cls_fpga_probe,
	.remove = cls_fpga_remove,
};

module_pci_driver(clsswbrd_pci_driver);

MODULE_AUTHOR("Pradchaya P.<pphuchar@celestica.com>");
MODULE_AUTHOR("Pariwat L.<pleamsum@celestica.com>");
MODULE_DESCRIPTION("Celestica Silverstone2 switchboard driver");
MODULE_VERSION(MOD_VERSION);
MODULE_LICENSE("GPL");
