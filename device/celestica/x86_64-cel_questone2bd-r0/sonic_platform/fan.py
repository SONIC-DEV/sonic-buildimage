#!/usr/bin/env python

#############################################################################
# Celestica
#
# Module contains an implementation of SONiC Platform Base API and
# provides the fan status which are available in the platform
#
#############################################################################

import json
import math
import os.path

try:
    from sonic_platform_base.fan_base import FanBase
    from helper import APIHelper
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")


FAN_NAME_LIST = ["FAN-1F", "FAN-1R", "FAN-2F",
                 "FAN-2R", "FAN-3F", "FAN-3R", "FAN-4F", "FAN-4R"]

FAN_SYSFS_PATH = "/sys/bus/i2c/drivers/fancpld/66-000d/"
FAN_DIRECTION_BIT = 1
FAN_PRESENT_BIT = 1
FAN_RPM_MULTIPLIER = 150
FAN_MAX_PWM = 255
FAN_MAX_RPM = FAN_MAX_PWM * FAN_RPM_MULTIPLIER
FAN_PRESENT_SYSFS = "fan{}_present"
FAN_PWM_SYSFS = "fan{}_pwm"
FAN_DIRECTION_SYSFS = "fan{}_dir"
FAN_LED_SYSFS = "fan{}_led"
FAN_LED_GREEN_CMD = "0x1"
FAN_LED_RED_CMD = "0x2"
FAN_LED_OFF_CMD = "0x3"

PSU_FAN_MAX_RPM = 30000
PSU_HWMON_PATH = "/sys/bus/i2c/devices/i2c-{0}/{0}-00{1}/hwmon"
PSU_I2C_MAPPING = {
    0: {
        "i2c_num": 75,
        "pmbus_reg": "58",
    },
    1: {
        "i2c_num": 76,
        "pmbus_reg": "59",
    },
}

FAN_MUX_HWMON_PATH = "/sys/bus/i2c/devices/i2c-66/i2c-{0}/{0}-0050/"
PSU_MUX_HWMON_PATH = "/sys/bus/i2c/devices/i2c-68/i2c-{0}/{0}-0050/"


class Fan(FanBase):
    """Platform-specific Fan class"""

    def __init__(self, fan_tray_index, fan_index=0, is_psu_fan=False, psu_index=0):
        self.fan_index = fan_index
        self.fan_tray_index = fan_tray_index
        self.is_psu_fan = is_psu_fan
        if self.is_psu_fan:
            self.psu_index = psu_index
            self.psu_hwmon_path = PSU_HWMON_PATH.format(
                PSU_I2C_MAPPING[self.psu_index]["i2c_num"], PSU_I2C_MAPPING[self.psu_index]["pmbus_reg"])
        self._api_helper = APIHelper()
        self.index = self.fan_tray_index * 2 + self.fan_index

    def __read_fan_sysfs(self, sysfs_file):
        sysfs_path = os.path.join(FAN_SYSFS_PATH, sysfs_file)
        return self._api_helper.read_one_line_file(sysfs_path)

    def __write_fan_sysfs(self, sysfs_file, value):
        sysfs_path = os.path.join(FAN_SYSFS_PATH, sysfs_file)
        return self._api_helper.write_file(sysfs_path, value)

    def __search_dirpath_contain(self, directory, search_str, file_start):
        for dirpath, dirnames, files in os.walk(directory):
            for name in files:
                file_path = os.path.join(dirpath, name)
                if name.startswith(file_start) and search_str in self._api_helper.read_txt_file(file_path):
                    return dirpath
        return None

    def get_direction(self):
        """
        Retrieves the direction of fan
        Returns:
            A string, either FAN_DIRECTION_INTAKE or FAN_DIRECTION_EXHAUST
            depending on fan direction
        """
        if self.is_psu_fan:
            return self.FAN_DIRECTION_EXHAUST

        fan_direction_val = self.__read_fan_sysfs(
            FAN_DIRECTION_SYSFS.format(self.fan_tray_index+1))

        return self.FAN_DIRECTION_EXHAUST if fan_direction_val == "0x0" else self.FAN_DIRECTION_INTAKE

    def get_speed(self):
        """
        Retrieves the speed of fan as a percentage of full speed
        Returns:
            An integer, the percentage of full fan speed, in the range 0 (off)
                 to 100 (full speed)

        Note:
            M = 150
            Max = 38250 RPM
        """
        if self.is_psu_fan:
            speed = 0
            fan_name = "fan{}_input"
            label = "dps1100"

            hwmon = self.__search_dirpath_contain(
                self.psu_hwmon_path, label, "name")
            if hwmon:
                fan_path = os.path.join(
                    hwmon, fan_name.format(self.fan_index+1))
                speed_rpm = self._api_helper.read_one_line_file(fan_path)
                speed = int(float(speed_rpm) / PSU_FAN_MAX_RPM * 100)

            return speed

        speed_raw = self.__read_fan_sysfs(
            FAN_PWM_SYSFS.format(self.fan_tray_index+1))
        speed_val = int(speed_raw, 16)
        speed = int(float(speed_val)/FAN_MAX_PWM * 100)

        return speed

    def get_target_speed(self):
        """
        Retrieves the target (expected) speed of the fan
        Returns:
            An integer, the percentage of full fan speed, in the range 0 (off)
                 to 100 (full speed)

        Note:
            speed_pc = pwm_target/255*100

            0   : when PWM mode is use
            pwm : when pwm mode is not use
        """
        target = 0
        return target

    def get_speed_tolerance(self):
        """
        Retrieves the speed tolerance of the fan
        Returns:
            An integer, the percentage of variance from target speed which is
                 considered tolerable
        """
        return 10

    def set_speed(self, speed):
        """
        Sets the fan speed
        Args:
            speed: An integer, the percentage of full fan speed to set fan to,
                   in the range 0 (off) to 100 (full speed)
        Returns:
            A boolean, True if speed is set successfully, False if not
        Notes:
            pwm setting mode must set as Manual
            manual: systemctl stop fanctrl.service
            auto: systemctl start fanctrl.service
        """

        if self.is_psu_fan:
            # Not support
            return False

        speed_hex = hex(int(float(speed)/100 * 255))
        return self.__write_fan_sysfs(FAN_PWM_SYSFS.format(self.fan_tray_index+1), speed_hex)

    def set_status_led(self, color):
        """
        Sets the state of the fan module status LED
        Args:
            color: A string representing the color with which to set the
                   fan module status LED
        Returns:
            bool: True if status LED state is set successfully, False if not
        """

        if self.is_psu_fan:
            # Not support
            return False

        led_cmd = {
            self.STATUS_LED_COLOR_GREEN: FAN_LED_GREEN_CMD,
            self.STATUS_LED_COLOR_RED: FAN_LED_RED_CMD,
            self.STATUS_LED_COLOR_OFF: FAN_LED_OFF_CMD
        }.get(color)

        return self.__write_fan_sysfs(FAN_LED_SYSFS.format(self.fan_tray_index+1), led_cmd)

    def get_status_led(self):
        """
        Gets the state of the fan status LED
        Returns:
            A string, one of the predefined STATUS_LED_COLOR_* strings above

        Note:
            Output
            STATUS_LED_COLOR_GREEN = "green"
            STATUS_LED_COLOR_AMBER = "amber"
            STATUS_LED_COLOR_RED = "red"
            STATUS_LED_COLOR_OFF = "off"

            Input
            0x1: green
            0x2: red
            0x3: off
        """
        if self.is_psu_fan:
            # Not support
            return self.STATUS_LED_COLOR_OFF

        fan_led_raw = self.__read_fan_sysfs(
            FAN_LED_SYSFS.format(self.fan_tray_index+1))

        fan_status_led = {
            "0x1": self.STATUS_LED_COLOR_GREEN,
            "0x2": self.STATUS_LED_COLOR_RED,
            "0x3": self.STATUS_LED_COLOR_OFF
        }.get(fan_led_raw, self.STATUS_LED_COLOR_OFF)

        return fan_status_led

    ##############################################################
    ###################### Device methods ########################
    ##############################################################

    def get_name(self):
        """
        Retrieves the name of the device
            Returns:
            string: The name of the device
        """
        fan_name = FAN_NAME_LIST[self.fan_tray_index*2 + self.fan_index] if not self.is_psu_fan else "PSU-{} FAN-{}".format(
            self.psu_index+1, self.fan_index+1)

        return fan_name

    def get_presence(self):
        """
        Retrieves the presence of the FAN
        Returns:
            bool: True if FAN is present, False if not
        """
        if self.is_psu_fan:
            return True

        fan_presence_bit_val = "0"

        return True if fan_presence_bit_val == "0" else False

    def get_model(self):
        """
        Retrieves the model number (or part number) of the device
        Returns:
            string: Model/part number of device
        """
        if self.is_psu_fan:
            temp_file = PSU_MUX_HWMON_PATH.format((self.fan_tray_index+1) + 75)
            return self._api_helper.fru_decode_product_model(self._api_helper.read_eeprom_sysfs(temp_file, "eeprom"))

        temp_file = FAN_MUX_HWMON_PATH.format((self.fan_tray_index+1) * 2)
        return self._api_helper.fru_decode_product_model(self._api_helper.read_eeprom_sysfs(temp_file, "eeprom"))

    def get_serial(self):
        """
        Retrieves the serial number of the device
        Returns:
            string: Serial number of device
        """
        if self.is_psu_fan:
            temp_file = PSU_MUX_HWMON_PATH.format((self.fan_tray_index+1) + 75)
            return self._api_helper.fru_decode_product_serial(self._api_helper.read_eeprom_sysfs(temp_file, "eeprom"))

        temp_file = FAN_MUX_HWMON_PATH.format((self.fan_tray_index+1) * 2)
        return self._api_helper.fru_decode_product_serial(self._api_helper.read_eeprom_sysfs(temp_file, "eeprom"))

    def get_status(self):
        """
        Retrieves the operational status of the device
        Returns:
            A boolean value, True if device is operating properly, False if not
        """
        return self.get_presence() and self.get_speed() > 0
