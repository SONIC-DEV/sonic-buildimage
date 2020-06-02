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
    PORT_END = 40
    QSFP_PORT_START = 1
    QSFP_PORT_END = 40

    EEPROM_OFFSET = 9
    PORT_INFO_PATH = '/sys/class/fishbone2_fpga'

    _port_name = ""
    _port_to_eeprom_mapping = {
      1: "/sys/bus/i2c/devices/i2c-42/42-0050/eeprom", 21: "/sys/bus/i2c/devices/i2c-46/46-0050/eeprom",
      2: "/sys/bus/i2c/devices/i2c-43/43-0050/eeprom", 22: "/sys/bus/i2c/devices/i2c-47/47-0050/eeprom",
      3: "/sys/bus/i2c/devices/i2c-10/10-0050/eeprom", 23: "/sys/bus/i2c/devices/i2c-26/26-0050/eeprom",
      4: "/sys/bus/i2c/devices/i2c-11/11-0050/eeprom", 24: "/sys/bus/i2c/devices/i2c-27/27-0050/eeprom",
      5: "/sys/bus/i2c/devices/i2c-12/12-0050/eeprom", 25: "/sys/bus/i2c/devices/i2c-28/28-0050/eeprom",
      6: "/sys/bus/i2c/devices/i2c-13/13-0050/eeprom", 26: "/sys/bus/i2c/devices/i2c-29/29-0050/eeprom",
      7: "/sys/bus/i2c/devices/i2c-14/14-0050/eeprom", 27: "/sys/bus/i2c/devices/i2c-30/30-0050/eeprom",
      8: "/sys/bus/i2c/devices/i2c-15/15-0050/eeprom", 28: "/sys/bus/i2c/devices/i2c-31/31-0050/eeprom",
      9: "/sys/bus/i2c/devices/i2c-16/16-0050/eeprom", 29: "/sys/bus/i2c/devices/i2c-32/32-0050/eeprom",
      10: "/sys/bus/i2c/devices/i2c-17/17-0050/eeprom", 30: "/sys/bus/i2c/devices/i2c-33/33-0050/eeprom",
      11: "/sys/bus/i2c/devices/i2c-18/18-0050/eeprom", 31: "/sys/bus/i2c/devices/i2c-34/34-0050/eeprom",
      12: "/sys/bus/i2c/devices/i2c-19/19-0050/eeprom", 32: "/sys/bus/i2c/devices/i2c-35/35-0050/eeprom",
      13: "/sys/bus/i2c/devices/i2c-20/20-0050/eeprom", 33: "/sys/bus/i2c/devices/i2c-36/36-0050/eeprom",
      14: "/sys/bus/i2c/devices/i2c-21/21-0050/eeprom", 34: "/sys/bus/i2c/devices/i2c-37/37-0050/eeprom",
      15: "/sys/bus/i2c/devices/i2c-22/22-0050/eeprom", 35: "/sys/bus/i2c/devices/i2c-38/38-0050/eeprom",
      16: "/sys/bus/i2c/devices/i2c-23/23-0050/eeprom", 36: "/sys/bus/i2c/devices/i2c-39/39-0050/eeprom",
      17: "/sys/bus/i2c/devices/i2c-24/24-0050/eeprom", 37: "/sys/bus/i2c/devices/i2c-40/40-0050/eeprom",
      18: "/sys/bus/i2c/devices/i2c-25/25-0050/eeprom", 38: "/sys/bus/i2c/devices/i2c-41/41-0050/eeprom",
      19: "/sys/bus/i2c/devices/i2c-44/44-0050/eeprom", 39: "/sys/bus/i2c/devices/i2c-48/48-0050/eeprom",
      20: "/sys/bus/i2c/devices/i2c-45/45-0050/eeprom", 40: "/sys/bus/i2c/devices/i2c-49/49-0050/eeprom",
    }
    _port_to_i2cbus_mapping = {
      1: 42, 2: 43, 3: 10, 4: 11, 5: 12, 6: 13, 7: 14, 8: 15, 9: 16, 10: 17,
      11: 18, 12: 19, 13: 20, 14: 21, 15: 22, 16: 23, 17: 24, 18: 25, 19: 44, 20: 45,
      21: 46, 22: 47, 23: 26, 24: 27, 25: 28, 26: 29, 27: 30, 28: 31, 29: 32, 30: 33,
      31: 34, 32: 35, 33: 36, 34: 37, 35: 38, 36: 39, 37: 40, 38: 41, 39: 48, 40: 49,
    }

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

    @property
    def port_to_i2cbus_mapping(self):
        return self._port_to_i2cbus_mapping

    def get_port_name(self, port_num):
        if port_num in self.qsfp_ports:
            self._port_name = "QSFP" + str(port_num - self.QSFP_PORT_START + 1)
        else:
            self._port_name = "SFP" + str(port_num)
        return self._port_name

    def get_eeprom_dom_raw(self, port_num):
        if port_num in self.qsfp_ports:
            # QSFP DOM EEPROM is also at addr 0x50 and thus also stored in eeprom_ifraw
            return None
        else:
            # Read dom eeprom at addr 0x51
            return self._read_eeprom_devid(port_num, self.DOM_EEPROM_ADDR, 256)

    def __init__(self):
        # Override port_to_eeprom_mapping for class initialization
        eeprom_path = '/sys/bus/i2c/devices/i2c-{0}/{0}-0050/eeprom'
        # the following scheme is not correct,use 'i2cdetect -y -l' to detect #
        #for x in range(self.PORT_START, self.PORT_END+1):
        #    self.port_to_i2cbus_mapping[x] = (x + self.EEPROM_OFFSET)
        #    self.port_to_eeprom_mapping[x] = eeprom_path.format(
        #        x + self.EEPROM_OFFSET)
        print("self.port_to_i2cbus_mapping: "+str(self.port_to_i2cbus_mapping)+"\n")
        print("self.port_to_eeprom_mapping: "+str(self.port_to_eeprom_mapping)+"\n")

        SfpUtilBase.__init__(self)

    def get_presence(self, port_num):

        # Check for invalid port_num
        if port_num not in range(self.port_start, self.port_end + 1):
            return False

        # Get path for access port presence status
        port_name = self.get_port_name(port_num)
        sysfs_filename = "qsfp_modprs" if port_num in self.qsfp_ports else "sfp_modabs"
        reg_path = "/".join([self.PORT_INFO_PATH, port_name, sysfs_filename])

        # Read status
        try:
            reg_file = open(reg_path)
            content = reg_file.readline().rstrip()
            reg_value = int(content)
        except IOError as e:
            print "Error: unable to open file: %s" % str(e)
            return False

        # Module present is active low
        if reg_value == 0:
            return True

        return False

    def get_low_power_mode(self, port_num):
        return NotImplementedError

    def set_low_power_mode(self, port_num, lpmode):
        # Check for invalid QSFP port_num
        if port_num not in self.qsfp_ports:
            return False

        try:
            port_name = self.get_port_name(port_num)
            reg_file = open(
                "/".join([self.PORT_INFO_PATH, port_name, "qsfp_lpmode"]), "r+")
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
        if port_num not in self.qsfp_ports:
            return False

        try:
            port_name = self.get_port_name(port_num)
            reg_file = open(
                "/".join([self.PORT_INFO_PATH, port_name, "qsfp_reset"]), "w")
        except IOError as e:
            print "Error: unable to open file: %s" % str(e)
            return False

        # Convert our register value back to a hex string and write back
        reg_file.seek(0)
        reg_file.write(hex(0))
        reg_file.close()

        # Sleep 1 second to allow it to settle
        time.sleep(1)

        # Flip the bit back high and write back to the register to take port out of reset
        try:
            reg_file = open(
                "/".join([self.PORT_INFO_PATH, port_name, "qsfp_reset"]), "w")
        except IOError as e:
            print "Error: unable to open file: %s" % str(e)
            return False

        reg_file.seek(0)
        reg_file.write(hex(1))
        reg_file.close()

        return True

    def get_transceiver_change_event(self, timeout=0):
        """
        TBD
        """
        return NotImplementedError

    def tx_disable(self, port_num, disable):
        """
        @param port_num index of physical port
        @param disable, True  -- disable port tx signal
                        False -- enable port tx signal
        @return True when operation success, False on failure.
        """
        TX_DISABLE_BYTE_OFFSET = 86
        if port_num not in range(self.port_start, self.port_end + 1) or type(disable) != bool:
            return False

        # QSFP, set eeprom to disable tx
        if port_num in self.qsfp_ports:
            presence = self.get_presence(port_num)
            if not presence:
                return True

            disable = b'\x0f' if disable else b'\x00'
            # open eeprom
            try:
                with open(self.port_to_eeprom_mapping[port_num], mode="wb", buffering=0) as sysfsfile:
                    sysfsfile.seek(TX_DISABLE_BYTE_OFFSET)
                    sysfsfile.write(bytearray(disable))
            except IOError:
                return False
            except:
                return False

        # SFP, set tx_disable pin
        else:
            try:
                disable = hex(1) if disable else hex(0)
                port_name = self.get_port_name(port_num)
                reg_file = open(
                    "/".join([self.PORT_INFO_PATH, port_name, "sfp_txdisable"]), "w")
                reg_file.write(disable)
                reg_file.close()
            except IOError as e:
                print "Error: unable to open file: %s" % str(e)
                return False

        return True
