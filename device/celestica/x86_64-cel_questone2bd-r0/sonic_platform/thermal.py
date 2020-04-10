#!/usr/bin/env python

#############################################################################
# Celestica
#
# Thermal contains an implementation of SONiC Platform Base API and
# provides the thermal device status which are available in the platform
#
#############################################################################

import json
import math
import os.path

try:
    from sonic_platform_base.thermal_base import ThermalBase
    from helper import APIHelper
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")

SENSORS_HWMON_PATH = "/sys/bus/i2c/devices/i2c-{0}/{0}-00{1}"
SENSORS_MUX_HWMON_PATH = "/sys/bus/i2c/devices/i2c-{0}/i2c-{1}/{1}-00{2}"

DEFAULT_VAL = 0.0

class Thermal(ThermalBase):
    """Platform-specific Thermal class"""

    def __init__(self, thermal_index):
        ThermalBase.__init__(self)
        self.thermal_index = thermal_index
        self._api_helper = APIHelper()

        ######  Thermal list defined #######
        # (NAME ,  Hwmon name , I2CBUS, I2C_MUX, I2C Address, T/F I2C_MUX, Temp_NUM)
        self.THERMAL_LIST = [
            ('TEMP_lm75b_i2c_9_48', 'lm75b',  66, 9, '48', True, 1),
            ('TEMP_lm75b_i2c_67_4d',    'lm75b',  67, 0, '4d', False, 1),
            ('TEMP_dps1100_i2c_75_58_1',    'dps1100',  75, 0, '58', False, 1),
            ('TEMP_dps1100_i2c_75_58_2',    'dps1100',  75, 0, '58', False, 2),
            ('TEMP_dps1100_i2c_76_59_1',    'dps1100',  76, 0, '59', False, 1),
            ('TEMP_dps1100_i2c_76_59_2',    'dps1100',  76, 0, '59', False, 2),
            ('TEMP_syscpld_i2c_70_0d',    'syscpld',  70, 0, '0d', False, 1),
            ('TEMP_jc42_i2c_1_18',    'jc42',  1, 0, '18', False, 1),
            ('TEMP_core_tmp_1',    'coretemp',  0, 0, '00', False, 1),
            ('TEMP_core_tmp_2',    'coretemp',  0, 0, '00', False, 2),
            ('TEMP_core_tmp_3',    'coretemp',  0, 0, '00', False, 3),
            ('TEMP_core_tmp_4',    'coretemp',  0, 0, '0', False, 4),
        ]

        if self.THERMAL_LIST[self.thermal_index][1] == 'coretemp':
            # Specifi check coretemp condition, Because Hwmon path is not standard.
            self.hwmon_path = "/sys/bus/platform/drivers/coretemp/coretemp.0/"
        else:
            if self.THERMAL_LIST[self.thermal_index][5]:
                self.hwmon_path = SENSORS_MUX_HWMON_PATH.format(
                    self.THERMAL_LIST[self.thermal_index][2],
                    self.THERMAL_LIST[self.thermal_index][3],
                    self.THERMAL_LIST[self.thermal_index][4])
            else:
                self.hwmon_path = SENSORS_HWMON_PATH.format(
                    self.THERMAL_LIST[self.thermal_index][2],
                    self.THERMAL_LIST[self.thermal_index][4])
        self.ss_index = self.THERMAL_LIST[self.thermal_index][6]

        # Search directory
        label = self.THERMAL_LIST[self.thermal_index][1]
        self.hwmon_path = self.__search_dirpath_contain(
            self.hwmon_path, label, "name")

    def __search_dirpath_contain(self, directory, search_str, file_start):
        # Searching file_start from current directory inclunding sub-directory.
        self.dirpath = []
        for dirpath, dirnames, files in os.walk(directory):
            for name in files:
                file_path = os.path.join(dirpath, name)
                if name.startswith(file_start) and search_str in self._api_helper.read_txt_file(file_path):
                    self.dirpath.append(dirpath)
        return self.dirpath

    def __get_temp(self, temp_file):
        for hwmon_path in self.dirpath:
            try:
                temp_file_path = os.path.join(hwmon_path, temp_file)
                raw_temp = self._api_helper.read_txt_file(temp_file_path)
                temp = float(raw_temp)/1000
                return "{:.3f}".format(temp)
            except:
                continue
        return DEFAULT_VAL

    def __set_threshold(self, file_name, temperature):
        for hwmon_path in self.dirpath:
            temp_file_path = os.path.join(hwmon_path, file_name)
            try:
                with open(temp_file_path, 'w') as fd:
                    fd.write(str(temperature))
                return True
            except IOError:
                continue
        return False

    def get_temperature(self):
        """
        Retrieves current temperature reading from thermal
        Returns:
            A float number of current temperature in Celsius up to nearest thousandth
            of one degree Celsius, e.g. 30.125
        """
        temp_file = "temp{}_input".format(self.ss_index)
        return self.__get_temp(temp_file)

    def get_high_threshold(self):
        """
        Retrieves the high threshold temperature of thermal
        Returns:
            A float number, the high threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        temp_file = "temp{}_max".format(self.ss_index)
        return self.__get_temp(temp_file)

    def set_high_threshold(self, temperature):
        """
        Sets the high threshold temperature of thermal
        Args :
            temperature: A float number up to nearest thousandth of one degree Celsius,
            e.g. 30.125
        Returns:
            A boolean, True if threshold is set successfully, False if not
        """

        temp_file = "temp{}_max".format(self.ss_index)
        is_set = self.__set_threshold(temp_file, int(temperature*1000))
        return is_set

    def get_low_threshold(self):
        """
        Retrieves the high threshold temperature of thermal
        Returns:
            A float number, the high threshold temperature of thermal in Celsius
            up to nearest thousandth of one degree Celsius, e.g. 30.125
        """
        temp_min_file = []
        temp_min_file.append("temp{}_min".format(self.ss_index))
        temp_min_file.append("temp{}_max_hyst".format(self.ss_index))

        for temp_file in temp_min_file:
            result = self.__get_temp(temp_file)
            if result is not False:
                break
        return result

    def set_low_threshold(self, temperature):
        """
        Sets the high threshold temperature of thermal
        Args :
            temperature: A float number up to nearest thousandth of one degree Celsius,
            e.g. 30.125
        Returns:
            A boolean, True if threshold is set successfully, False if not
        """
        temp_min_file = []
        temp_min_file.append("temp{}_min".format(self.ss_index))
        temp_min_file.append("temp{}_max_hyst".format(self.ss_index))

        for temp_file in temp_min_file:
            result = self.__set_threshold(temp_file, int(temperature*1000))
            if result is not False:
                break
        return result

    ##############################################################
    ###################### Device methods ########################
    ##############################################################

    def get_name(self):
        """
        Retrieves the name of the thermal device
            Returns:
            string: The name of the thermal device
        """
        return self.THERMAL_LIST[self.thermal_index][0]

    def get_presence(self):
        """
        Retrieves the presence of the PSU
        Returns:
            bool: True if PSU is present, False if not
        """
        for hwmon_path in self.dirpath:
            try:
                temp_file = "temp{}_input".format(self.ss_index)
                temp_file_path = os.path.join(hwmon_path, temp_file)
                if os.path.isfile(temp_file_path):
                    return True
            except:
                continue
        return False

    def get_model(self):
        """
        Retrieves the model number (or part number) of the device
        Returns:
            string: Model/part number of device
        """
        return self.THERMAL_LIST[self.thermal_index][1]

    def get_serial(self):
        """
        Retrieves the serial number of the device
        Returns:
            string: Serial number of device
        """
        return "N/A"

    def get_status(self):
        """
        Retrieves the operational status of the device
        Returns:
            A boolean value, True if device is operating properly, False if not
        """
        if not self.get_presence():
            return False

        for hwmon_path in self.dirpath:
            try:
                fault_file = "temp{}_fault".format(self.ss_index)
                fault_file_path = os.path.join(hwmon_path, fault_file)
                if not os.path.isfile(fault_file_path):
                    return True

                raw_txt = self._api_helper.read_txt_file(fault_file_path)
                return int(raw_txt) == 0
            except:
                continue
        return False
