#!/usr/bin/env python

#############################################################################
# Celestica
#
# Module contains an implementation of SONiC Platform Base API and
# provides the Chassis information which are available in the platform
#
#############################################################################

try:
    import json
    import os
    import re
    import sys
    import subprocess
    import time
    from sonic_platform_base.chassis_base import ChassisBase
    from helper import APIHelper
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")

NUM_FAN_TRAY = 5
NUM_FAN = 2
NUM_PSU = 4
NUM_THERMAL = 25
NUM_SFP = 128
NUM_COMPONENT = 7

#SFP_PORT_START = 1
#SFP_PORT_END = 48
QSFP_PORT_START = 1
QSFP_PORT_END = 128

REBOOT_CAUSE_REG = "0xA106"
TLV_EEPROM_I2C_BUS = 0
TLV_EEPROM_I2C_ADDR = 56
BASE_CPLD_PLATFORM = "mt3010.cpldb"
BASE_GETREG_PATH = "/sys/devices/platform/{}/getreg".format(BASE_CPLD_PLATFORM)


class Chassis(ChassisBase):
    """Platform-specific Chassis class"""

    def __init__(self):
        ChassisBase.__init__(self)
        self._api_helper = APIHelper()
        self.sfp_module_initialized = False
        self.fan_module_initialized = False
        self.__initialize_eeprom()
        
        if self._api_helper.is_host():
            self.__initialize_fan()
            self.__initialize_psu()
            self.__initialize_thermals()
            #self.__initialize_components()

    def __initialize_sfp(self):
        from sonic_platform.sfp import Sfp
        for index in range(0, NUM_SFP):
            sfp = Sfp(index)
            self._sfp_list.append(fp)
        self.sfp_module_initialized = True

    def __initialize_psu(self):
        from sonic_platform.psu import Psu
        for index in range(0, NUM_PSU):
            psu = Psu(index)
            self._psu_list.append(psu)

    def __initialize_fan(self):
        from sonic_platform.fan import Fan
        for fant_index in range(0, NUM_FAN_TRAY):
            for fan_index in range(0, NUM_FAN):
                fan = Fan(fant_index, fan_index)
                self._fan_list.append(fan)
        self.fan_module_initialized = True

    def __initialize_thermals(self):
        from sonic_platform.thermal import Thermal
        for index in range(0, NUM_THERMAL):
            thermal = Thermal(index)
            self._thermal_list.append(thermal)

    def __initialize_eeprom(self):
        from sonic_platform.eeprom import Eeprom
        self._eeprom = Eeprom(TLV_EEPROM_I2C_BUS, TLV_EEPROM_I2C_ADDR)
    
    def __initialize_components(self):
        from sonic_platform.component import Component
        for index in range(0, NUM_COMPONENT):
            component = Component(index)
            self._component_list.append(component)
    
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
        hx_cause = self._api_helper.get_register_value(
            BASE_GETREG_PATH, REBOOT_CAUSE_REG)

        return {
            "0x00": (self.REBOOT_CAUSE_HARDWARE_OTHER, 'Unknown'),
            "0x11": (self.REBOOT_CAUSE_POWER_LOSS, 'The last reset is Power on reset'),
            "0x22": (self.REBOOT_CAUSE_HARDWARE_OTHER, 'The last reset is soft-set CPU warm reset'),
            "0x33": (self.REBOOT_CAUSE_HARDWARE_OTHER, 'The last reset is soft-set CPU cold reset'),
            "0x44": (self.REBOOT_CAUSE_HARDWARE_OTHER, 'The last reset is CPU warm reset'),
            "0x55": (self.REBOOT_CAUSE_HARDWARE_OTHER, 'The last reset is CPU cold reset'),
            "0x66": (self.REBOOT_CAUSE_WATCHDOG, 'The last reset is watchdog reset'),
            "0x77": (self.REBOOT_CAUSE_HARDWARE_OTHER, 'The last reset is power cycle reset'),
        }.get(hx_cause.lower(), (self.REBOOT_CAUSE_HARDWARE_OTHER, 'Unknown'))

    ##############################################################
    ######################## SFP methods #########################
    ##############################################################

    def get_num_sfps(self):
        """
        Retrieves the number of sfps available on this chassis
        Returns:
            An integer, the number of sfps available on this chassis
        """
        if not self.sfp_module_initialized:
            self.__initialize_sfp()

        return len(self._sfp_list)

    def get_all_sfps(self):
        """
        Retrieves all sfps available on this chassis
        Returns:
            A list of objects derived from SfpBase representing all sfps
            available on this chassis
        """
        if not self.sfp_module_initialized:
            self.__initialize_sfp()

        return self._sfp_list

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
        if not self.sfp_module_initialized:
            self.__initialize_sfp()

        try:
            # The index will start from 1
            sfp = self._sfp_list[index-1]
        except IndexError:
            sys.stderr.write("SFP index {} out of range (1-{})\n".format(
                             index, len(self._sfp_list)))
        return sfp

    ##############################################################
    ####################### Other methods ########################
    ##############################################################

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
        return self._api_helper.hwsku

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

    ##############################################################
    ###################### Event methods #########################
    ##############################################################

    def get_change_event(self, timeout=0):
        """
        Returns a nested dictionary containing all devices which have
        experienced a change at chassis level

        Args:
            timeout: Timeout in milliseconds (optional). If timeout == 0,
                this method will block until a change is detected.

        Returns:
            (bool, dict):
                - bool: True if call successful, False if not;
                - dict: A nested dictionary where key is a device type,
                        value is a dictionary with key:value pairs in the format of
                        {'device_id':'device_event'}, where device_id is the device ID
                        for this device and device_event.
                        The known devices's device_id and device_event was defined as table below.
                         -----------------------------------------------------------------
                         device   |     device_id       |  device_event  |  annotate
                         -----------------------------------------------------------------
                         'fan'          '<fan number>'     '0'              Fan removed
                                                           '1'              Fan inserted

                         'sfp'          '<sfp number>'     '0'              Sfp removed
                                                           '1'              Sfp inserted
                                                           '2'              I2C bus stuck
                                                           '3'              Bad eeprom
                                                           '4'              Unsupported cable
                                                           '5'              High Temperature
                                                           '6'              Bad cable

                         'voltage'      '<monitor point>'  '0'              Vout normal
                                                           '1'              Vout abnormal
                         --------------------------------------------------------------------
                  Ex. {'fan':{'0':'0', '2':'1'}, 'sfp':{'11':'0', '12':'1'},
                       'voltage':{'U20':'0', 'U21':'1'}}
                  Indicates that:
                     fan 0 has been removed, fan 2 has been inserted.
                     sfp 11 has been removed, sfp 12 has been inserted.
                     monitored voltage U20 became normal, voltage U21 became abnormal.
                  Note: For sfp, when event 3-6 happened, the module will not be avalaible,
                        XCVRD shall stop to read eeprom before SFP recovered from error status.
        """
        from sonic_platform.event import FanEvent, SfpEvent, VoltageEvent, POLL_INTERVAL

        if not self.fan_module_initialized:
            self.__initialize_fan()

        if not self.sfp_module_initialized:
            self.__initialize_sfp()

        fan_event = FanEvent(self._fan_list)
        sfp_event = SfpEvent(self._sfp_list)
        voltage_event = VoltageEvent()

        cur_fan_state = fan_event.get_fan_state()
        cur_volt_state = voltage_event.get_voltage_state()
        start_milli_time = int(round(time.time() * 1000))
        int_sfp, int_fan, int_volt = {}, {}, {}

        sleep_time = min(
            timeout, POLL_INTERVAL) if timeout != 0 else POLL_INTERVAL
        while True:
            chk_sfp = sfp_event.check_all_port_interrupt_event()
            int_sfp = sfp_event.update_port_event_object(
                chk_sfp, int_sfp) if chk_sfp else int_sfp
            int_fan = fan_event.check_fan_status(cur_fan_state, int_fan)
            int_volt = voltage_event.check_voltage_status(cur_volt_state, int_volt)

            current_milli_time = int(round(time.time() * 1000))
            if (int_sfp or int_fan or int_volt) or \
                    (timeout != 0 and current_milli_time - start_milli_time > timeout):
                break

            time.sleep(sleep_time)

        change_dict = dict()
        change_dict['fan'] = int_fan
        change_dict['sfp'] = int_sfp
        change_dict['voltage'] = int_volt

        return True, change_dict
