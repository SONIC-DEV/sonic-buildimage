#!/usr/bin/env python

#############################################################################
# Celestica
#
# Module contains an implementation of SONiC Platform Base API and
# provides the PSUs status which are available in the platform
#
#############################################################################

try:
    import traceback
    import os
    import re
    import math
    from sonic_platform_base.psu_base import PsuBase
    from sonic_platform.eeprom import Eeprom
    from sonic_platform.fan import Fan
    from helper import APIHelper
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")

PSU_NUM_FAN = [1, 1]

PRESENT_BIT = '0x0'
POWER_OK_BIT = '0x1'
PSU_INFO_MAPPING = {
    0: {
        "name": "PSU-1",
	"psu_idx": "r",
        "i2c_num": 76,
        "pmbus_reg": "59",
        "eeprom_reg": "51",
    },
    1: {
        "name": "PSU-2",
	"psu_idx": "l",
        "i2c_num": 75,
        "pmbus_reg": "58",
        "eeprom_reg": "50"
    },
}
HWMON_PATH = "/sys/bus/i2c/devices/i2c-{0}/{0}-00{1}/hwmon"
CREATE_HWMON = "echo dps1100 0x{1} > /sys/bus/i2c/devices/i2c-{0}/new_device"
DELETE_HWMON = "echo 0x{1} > /sys/bus/i2c/devices/i2c-{0}/delete_device"
I2C_PATH = "/sys/bus/i2c/devices/i2c-{0}/{0}-00{1}/"
PSU_POWER_DIVIDER = 1000000
PSU_VOLT_DIVIDER = 1000
PSU_CUR_DIVIDER = 1000
PSU_SYSFS_PATH = "/sys/bus/i2c/drivers/syscpld/70-000d"
PSU_PRESENT_SYSFS = "psu_{}_present"
PSU_STATUS_SYSFS = "psu_{}_status"

PSU_MUX_HWMON_PATH = "/sys/bus/i2c/devices/i2c-68/i2c-{0}/{0}-00{1}/"

class Psu(PsuBase):
    """Platform-specific Psu class"""

    def __init__(self, psu_index):
        PsuBase.__init__(self)
        self.index = psu_index
        self.hwmon_path = HWMON_PATH.format(
            PSU_INFO_MAPPING[self.index]["i2c_num"], PSU_INFO_MAPPING[self.index]["pmbus_reg"])
        self.eeprom_path = I2C_PATH.format(
            PSU_INFO_MAPPING[self.index]["i2c_num"], PSU_INFO_MAPPING[self.index]["eeprom_reg"])
        self._api_helper = APIHelper()
        for fan_index in range(0, PSU_NUM_FAN[self.index]):
            fan = Fan(fan_index, 0, is_psu_fan=True, psu_index=self.index,
                      psu_fan_direction=self.__get_fan_direction())
            self._fan_list.append(fan)

    def __get_fan_direction(self):
        # DPS-1100FB = Intake
        # DPS-1100AB = exhaust
        fru_pn = self._api_helper.fru_decode_product_name(
            self._api_helper.read_eeprom_sysfs(self.eeprom_path, "eeprom"))
        return Fan.FAN_DIRECTION_INTAKE if "FB" in fru_pn \
            else Fan.FAN_DIRECTION_EXHAUST if "AB" in fru_pn \
                else "N/A"

    def __search_file_by_contain(self, directory, search_str, file_start):
        for dirpath, dirnames, files in os.walk(directory):
            for name in files:
                file_path = os.path.join(dirpath, name)
                if name.startswith(file_start) and search_str in self._api_helper.read_txt_file(file_path):
                    return file_path
        return None

    def _read_psu_sysfs(self, sysfs_file):
	sysfs_path = os.path.join(PSU_SYSFS_PATH, sysfs_file)
	return self._api_helper.read_one_line_file(sysfs_path)
 
    # when system boot with only one psu, the other one's hwmon cant not create successfully
    # when add power to this psu, we need create hwmon manually
    # in questone2f, suit for two situation which are psu not present and power loss
    def create_hwmon(self):
	if os.path.exists(self.hwmon_path):
	    return None
	else:
	    os.system(DELETE_HWMON.format(PSU_INFO_MAPPING[self.index]["i2c_num"],
		PSU_INFO_MAPPING[self.index]["pmbus_reg"]))
	    os.system(CREATE_HWMON.format(PSU_INFO_MAPPING[self.index]["i2c_num"],
		PSU_INFO_MAPPING[self.index]["pmbus_reg"]))

    def get_voltage(self):
        """
        Retrieves current PSU voltage output
        Returns:
            A float number, the output voltage in volts,
            e.g. 12.1
        """
        psu_voltage = 0.0
        voltage_name = "in{}_input"
        voltage_label = "vout1"
	if not self.get_status():
	    return psu_voltage

	self.create_hwmon()
        vout_label_path = self.__search_file_by_contain(
            self.hwmon_path, voltage_label, "in")
        if vout_label_path:
            dir_name = os.path.dirname(vout_label_path)
            basename = os.path.basename(vout_label_path)
            in_num = filter(str.isdigit, basename)
            vout_path = os.path.join(
                dir_name, voltage_name.format(in_num))
            vout_val = self._api_helper.read_txt_file(vout_path)
	    if vout_val is None:
		return psu_voltage
	    else:
                psu_voltage = float(vout_val) / PSU_VOLT_DIVIDER

        return psu_voltage

    def get_current(self):
        """
        Retrieves present electric current supplied by PSU
        Returns:
            A float number, the electric current in amperes, e.g 15.4
        """
        psu_current = 0.0
        current_name = "curr{}_input"
        current_label = "iout1"

	if not self.get_status():
	    return psu_current

	self.create_hwmon()
        curr_label_path = self.__search_file_by_contain(
            self.hwmon_path, current_label, "cur")
        if curr_label_path:
            dir_name = os.path.dirname(curr_label_path)
            basename = os.path.basename(curr_label_path)
            cur_num = filter(str.isdigit, basename)
            cur_path = os.path.join(
                dir_name, current_name.format(cur_num))
            cur_val = self._api_helper.read_txt_file(cur_path)
	    if cur_val is None:
		return psu_current
	    else:
                psu_current = float(cur_val) / PSU_CUR_DIVIDER

        return psu_current

    def get_power(self):
        """
        Retrieves current energy supplied by PSU
        Returns:
            A float number, the power in watts, e.g. 302.6
        """
        psu_power = 0.0
        power_name = "power{}_input"
        power_label = "pout1"

	if not self.get_status():
	    return psu_power

	self.create_hwmon()
        pw_label_path = self.__search_file_by_contain(
            self.hwmon_path, power_label, "power")
        if pw_label_path:
            dir_name = os.path.dirname(pw_label_path)
            basename = os.path.basename(pw_label_path)
            pw_num = filter(str.isdigit, basename)
            pw_path = os.path.join(
                dir_name, power_name.format(pw_num))
            pw_val = self._api_helper.read_txt_file(pw_path)
	    if pw_val is None:
		return psu_power
	    else:
                psu_power = float(pw_val) / PSU_POWER_DIVIDER

        return psu_power

    def get_powergood_status(self):
        """
        Retrieves the powergood status of PSU
        Returns:
            A boolean, True if PSU has stablized its output voltages and passed all
            its internal self-tests, False if not.
        """
        return self.get_status()

    def set_status_led(self, color):
        """
        Sets the state of the PSU status LED
        Args:
            color: A string representing the color with which to set the PSU status LED
                   Note: Only support green and off
        Returns:
            bool: True if status LED state is set successfully, False if not
        """
        # NOT ALLOW

        return False

    def get_status_led(self):
        """
        Gets the state of the PSU status LED
        Returns:
            A string, one of the predefined STATUS_LED_COLOR_* strings above
        """
        return self.STATUS_LED_COLOR_OFF

    ##############################################################
    ###################### Device methods ########################
    ##############################################################

    def get_name(self):
        """
        Retrieves the name of the device
            Returns:
            string: The name of the device
        """
        return PSU_INFO_MAPPING[self.index]["name"]

    def get_presence(self):
        """
        Retrieves the presence of the PSU
        Returns:
            bool: True if PSU is present, False if not
        """
	psu_presence_bit_val = self._read_psu_sysfs(PSU_PRESENT_SYSFS.format(PSU_INFO_MAPPING[self.index]["psu_idx"]))
	return True if psu_presence_bit_val == PRESENT_BIT else False

    def get_model(self):
        """
        Retrieves the model number (or part number) of the device
        Returns:
            string: Model/part number of device
        """
        model = self._api_helper.fru_decode_product_model(self._api_helper.read_eeprom_sysfs(self.eeprom_path, "eeprom"))
        if model.isspace():
            return 'N/A'
        else:
            return model

    def get_serial(self):
        """
        Retrieves the serial number of the device
        Returns:
            string: Serial number of device
        """
        return self._api_helper.fru_decode_product_serial(self._api_helper.read_eeprom_sysfs(self.eeprom_path, "eeprom"))

    def get_status(self):
        """
        Retrieves the operational status of the device
        Returns:
            A boolean value, True if device is operating properly, False if not
        """
	psu_status_bit_val = self._read_psu_sysfs(PSU_STATUS_SYSFS.format(PSU_INFO_MAPPING[self.index]["psu_idx"]))
	return True if psu_status_bit_val == POWER_OK_BIT else False
