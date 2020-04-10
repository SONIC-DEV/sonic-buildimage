#!/usr/bin/env python

#############################################################################
# Celestica
#
# Module contains an implementation of SONiC Platform Base API and
# provides the Chassis information which are available in the platform
#
#############################################################################

try:
    import sys
    import re
    import os
    import subprocess
    import json
    from sonic_platform_base.chassis_base import ChassisBase
    from sonic_platform.component import Component
    from sonic_platform.eeprom import Eeprom
    from sonic_platform.fan import Fan
    from sonic_platform.sfp import Sfp
    from sonic_platform.psu import Psu
    from sonic_platform.thermal import Thermal
    from helper import APIHelper
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")

NUM_FAN_TRAY = 4
NUM_FAN = 2
NUM_PSU = 2
NUM_THERMAL = 12
NUM_SFP = 56
NUM_COMPONENT = 7
REBOOT_CAUSE_REG = "0xA106"
TLV_EEPROM_I2C_BUS = 0
TLV_EEPROM_I2C_ADDR = 56

BASE_CPLD_PLATFORM = "questone2bd.cpldb"
BASE_GETREG_PATH = "/sys/devices/platform/{}/getreg".format(BASE_CPLD_PLATFORM)


class Chassis(ChassisBase):
    """Platform-specific Chassis class"""

    def __init__(self):
        ChassisBase.__init__(self)
        self._eeprom = Eeprom(TLV_EEPROM_I2C_BUS, TLV_EEPROM_I2C_ADDR)
        self._api_helper = APIHelper()

        for index in range(0, NUM_PSU):
            psu = Psu(index)
            self._psu_list.append(psu)

        for fant_index in range(0, NUM_FAN_TRAY):
            for fan_index in range(0, NUM_FAN):
                fan = Fan(fant_index, fan_index)
                self._fan_list.append(fan)

        for index in range(0, NUM_SFP):
            sfp = Sfp(index)
            self._sfp_list.append(sfp)

        for index in range(0, NUM_COMPONENT):
            component = Component(index)
            self._component_list.append(component)

        for index in range(0, NUM_THERMAL):
            thermal = Thermal(index)
            self._thermal_list.append(thermal)

    def get_base_mac(self):
        """
        Retrieves the base MAC address for the chassis
        Returns:
            A string containing the MAC address in the format
            'XX:XX:XX:XX:XX:XX'
        """
        return self._eeprom.get_mac()

    def get_serial_number(self):
        """
        Retrieves the hardware serial number for the chassis
        Returns:
            A string containing the hardware serial number for this chassis.
        """
        return self._eeprom.get_serial()

    def get_system_eeprom_info(self):
        """
        Retrieves the full content of system EEPROM information for the chassis
        Returns:
            A dictionary where keys are the type code defined in
            OCP ONIE TlvInfo EEPROM format and values are their corresponding
            values.
        """
        return self._eeprom.get_eeprom()

    def get_reboot_cause(self):
        """
        Retrieves the cause of the previous reboot

        Returns:
            A tuple (string, string) where the first element is a string
            containing the cause of the previous reboot. This string must be
            one of the predefined strings in this class. If the first string
            is "REBOOT_CAUSE_HARDWARE_OTHER", the second string can be used
            to pass a description of the reboot cause.
        """

        raw_cause = self._api_helper.get_register_value(
            BASE_GETREG_PATH, REBOOT_CAUSE_REG)
        hx_cause = raw_cause.lower()
        reboot_cause = {
            "0x00": self.REBOOT_CAUSE_HARDWARE_OTHER,
            "0x11": self.REBOOT_CAUSE_POWER_LOSS,
            "0x22": self.REBOOT_CAUSE_NON_HARDWARE,
            "0x33": self.REBOOT_CAUSE_HARDWARE_OTHER,
            "0x44": self.REBOOT_CAUSE_NON_HARDWARE,
            "0x55": self.REBOOT_CAUSE_NON_HARDWARE,
            "0x66": self.REBOOT_CAUSE_WATCHDOG,
            "0x77": self.REBOOT_CAUSE_NON_HARDWARE
        }.get(hx_cause, self.REBOOT_CAUSE_HARDWARE_OTHER)

        description = {
            "0x00": "Unknown reason",
            "0x11": "The last reset is Power on reset",
            "0x22": "The last reset is soft-set CPU warm reset",
            "0x33": "The last reset is soft-set CPU cold reset",
            "0x44": "The last reset is CPU warm reset",
            "0x55": "The last reset is CPU cold reset",
            "0x66": "The last reset is watchdog reset",
            "0x77": "The last reset is power cycle reset"
        }.get(hx_cause, "Unknown reason")

        return (reboot_cause, description)

    def get_sfp(self, index):
        """
        Retrieves sfp represented by (1-based) index <index>
        Args:
            index: An integer, the index (1-based) of the sfp to retrieve.
            The index should be the sequence of a physical port in a chassis,
            starting from 1.
            For example, 1 for Ethernet0, 2 for Ethernet4 and so on.
        Returns:
            An object dervied from SfpBase representing the specified sfp
        """
        sfp = None

        try:
            # The index will start from 1
            sfp = self._sfp_list[index-1]
        except IndexError:
            sys.stderr.write("SFP index {} out of range (1-{})\n".format(
                             index, len(self._sfp_list)))
        return sfp

    def get_watchdog(self):
        """
        Retreives hardware watchdog device on this chassis
        Returns:
            An object derived from WatchdogBase representing the hardware
            watchdog device
        """
        if self._watchdog is None:
            from sonic_platform.watchdog import Watchdog
            self._watchdog = Watchdog()

        return self._watchdog

    ##############################################################
    ###################### Device methods ########################
    ##############################################################

    def get_name(self):
        """
        Retrieves the name of the device
            Returns:
            string: The name of the device
        """
        return self._eeprom.get_product()

    def get_presence(self):
        """
        Retrieves the presence of the PSU
        Returns:
            bool: True if PSU is present, False if not
        """
        return True

    def get_model(self):
        """
        Retrieves the model number (or part number) of the device
        Returns:
            string: Model/part number of device
        """
        return self._eeprom.get_pn()

    def get_serial(self):
        """
        Retrieves the serial number of the device
        Returns:
            string: Serial number of device
        """
        return self.get_serial_number()

    def get_status(self):
        """
        Retrieves the operational status of the device
        Returns:
            A boolean value, True if device is operating properly, False if not
        """
        return True
