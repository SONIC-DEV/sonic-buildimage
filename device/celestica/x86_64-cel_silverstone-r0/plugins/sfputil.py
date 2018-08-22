#!/usr/bin/env python
#
# Platform-specific SFP transceiver interface for SONiC
#

try:
    import time
    from sonic_sfp.sfputilbase import SfpUtilBase
except ImportError as e:
    raise ImportError("%s - required module not found" % str(e))


class SfpUtil(SfpUtilBase):
    """Platform-specific SfpUtil class"""

    PORT_START = 1
    PORT_END = 34
    QSFP_PORT_START = 1
    QSFP_PORT_END = 32

    EEPROM_OFFSET = 10

    BASE_SFF_PATH = "/sys/devices/platform/AS58128.switchboard/SFF/"

    _port_name = ""
    _port_to_eeprom_mapping = {}

    @property
    def port_start(self):
        return self.PORT_START

    @property
    def port_end(self):
        return self.PORT_END

    @property
    def qsfp_ports(self):
        return range(self.QSFP_PORT_START, self.QSFP_PORT_END + 1)

    @property
    def port_to_eeprom_mapping(self):
        return self._port_to_eeprom_mapping

    def get_port_name(self, port_num):
        if port_num <= self.QSFP_PORT_END:
            self._port_name = "QSFP" + str(port_num)
        else:
            self._port_name = "SFP" + str(port_num - self.QSFP_PORT_END)
        return self._port_name

    def __init__(self):
        # Override port_to_eeprom_mapping for class initialization
        eeprom_path = '/sys/bus/i2c/devices/i2c-{0}/{0}-0050/eeprom'

        for x in range(self.PORT_START, self.PORT_END + 1):
            self.port_to_eeprom_mapping[x] = eeprom_path.format(
                x + self.EEPROM_OFFSET - 1)
        SfpUtilBase.__init__(self)

    def get_presence(self, port_num):
        # Check for invalid port_num
        if port_num < self.port_start or port_num > self.port_end:
            return False

        sysfs_filename = "sfp_modabs"
        port_name = self.get_port_name(port_num)
        if port_num in self.qsfp_ports:
            sysfs_filename = "qsfp_modprs"
        try:

            reg_file = open(self.BASE_SFF_PATH +
                            port_name + "/" + sysfs_filename)

        except IOError as e:
            print "Error: unable to open file: %s" % str(e)
            return False

        # Read status
        content = reg_file.readline().rstrip()
        reg_value = int(content)

        # ModPrsL is active low
        if reg_value == 0:
            return True

        return False

    def get_low_power_mode(self, port_num):
        # Check for invalid QSFP port_num
        if port_num < self.port_start or port_num > self.port_end or port_num > self.QSFP_PORT_END:
            return False

        try:
            port_name = self.get_port_name(port_num)
            reg_file = open(self.BASE_SFF_PATH + port_name + "/qsfp_lpmode")
        except IOError as e:
            print "Error: unable to open file: %s" % str(e)
            return False

        # Read status
        content = reg_file.readline().rstrip()
        reg_value = int(content, 16)
        # ModPrsL is active low
        if reg_value == 0:
            return False

        return True

    def set_low_power_mode(self, port_num, lpmode):
        # Check for invalid QSFP port_num
        if port_num < self.port_start or port_num > self.port_end or port_num > self.QSFP_PORT_END:
            return False

        try:
            port_name = self.get_port_name(port_num)
            reg_file = open(self.BASE_SFF_PATH +
                            port_name + "/qsfp_lpmode", "r+")
        except IOError as e:
            print "Error: unable to open file: %s" % str(e)
            return False

        content = hex(lpmode)

        reg_file.seek(0)
        reg_file.write(content)
        reg_file.close()

        return True

    def reset(self, port_num):
        # Check for invalid QSFP port_num
        if port_num < self.port_start or port_num > self.port_end or port_num > self.QSFP_PORT_END:
            return False

        try:
            port_name = self.get_port_name(port_num)
            reg_file = open(self.BASE_SFF_PATH +
                            port_name + "/qsfp_reset", "w")
        except IOError as e:
            print "Error: unable to open file: %s" % str(e)
            return False

        # Convert our register value back to a hex string and write back
        reg_file.seek(0)
        reg_file.write(hex(0))
        reg_file.close()

        # Sleep 1 second to allow it to settle
        time.sleep(1)

        # Flip the bit back high and write back to the register to take port
        # out of reset
        try:
            reg_file = open(self.BASE_SFF_PATH +
                            port_name + "/qsfp_reset", "w")
        except IOError as e:
            print "Error: unable to open file: %s" % str(e)
            return False

        reg_file.seek(0)
        reg_file.write(hex(1))
        reg_file.close()

        return True
