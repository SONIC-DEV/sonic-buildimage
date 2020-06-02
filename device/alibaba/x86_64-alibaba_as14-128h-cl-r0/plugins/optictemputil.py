#!/usr/bin/env python

#
#  optictemputil.py
#
#  Platform-specific Optic module temperature Interface for SONiC
#

__author__ = 'Pradchaya P.<pphuchar@celestica.com>'
__author__ = 'Wirut G.<wgetbumr@celestica.com>'
__license__ = "GPL"
__version__ = "1.0.0"
__status__  = "Development"

import os
import sys
import binascii
import subprocess

class OpticTempUtil():
    """Platform-specific OpticTempUtil class"""

    def __init__(self):
        pass

    def read_eeprom_specific_bytes(self, sysfsfile_eeprom, offset, num_bytes):
        eeprom_raw = []
        for i in range(0, num_bytes):
            eeprom_raw.append("0x00")

        try:
            sysfsfile_eeprom.seek(offset)
            raw = sysfsfile_eeprom.read(num_bytes)
        except IOError:
            return None

        try:
            for n in range(0, num_bytes):
                eeprom_raw[n] = hex(ord(raw[n]))[2:].zfill(2)
        except:
            return None

        return eeprom_raw


    def twos_comp(self, num, bits):
        try:
            if ((num & (1 << (bits - 1))) != 0):
                num = num - (1 << bits)
            return num
        except:
            return 0


    def calc_temperature(self, cal_type, eeprom_data, offset, size):

        msb = int(eeprom_data[offset], 16)
        lsb = int(eeprom_data[offset + 1], 16)

        result = (msb << 8) | (lsb & 0xff)
        result = self.twos_comp(result, 16)

        if cal_type == 1:
        # Internal calibration
            result = float(result / 256.0)
            retval = '%.4f' %result

        # TODO: Should support external calibration in future.
        else:
            retval = 0

        return retval

    ''' TODO: Change busnum to sysfs_sfp_i2c_client_eeprom_path from caller!!!
    '''
    def get_optic_temp(self, sysfs_sfp_i2c_client_eeprom_path, port_type):

        EEPROM_ADDR = 0x50
        DOM_ADDR = 0x51
        EEPROM_OFFSET = 0
        DOM_OFFSET = 256

        SFP_DMT_ADDR = 92
        SFP_DMT_WIDTH = 1
        SFP_TEMP_DATA_ADDR = 96
        SFP_TEMP_DATA_WIDTH = 2

        QSFP_TEMP_DATA_ADDR = 22
        QSFP_TEMP_DATA_WIDTH = 2       
        temperature_raw = None


        ''' Open file here '''
        try:
            sysfsfile_eeprom = open(sysfs_sfp_i2c_client_eeprom_path, mode="rb", buffering=0)
        except IOError:
            print("Error: reading sysfs file %s" % sysfs_sfp_i2c_client_eeprom_path)
            return 0

        if port_type == 'QSFP':

            # QSFP only have internal calibration mode.
            cal_type = 1
            # read temperature raw value
            temperature_raw = self.read_eeprom_specific_bytes(sysfsfile_eeprom,(EEPROM_OFFSET+QSFP_TEMP_DATA_ADDR),QSFP_TEMP_DATA_WIDTH)
        else:
            # read calibration type at bit 5
            cal_type = self.read_eeprom_specific_bytes(sysfsfile_eeprom,EEPROM_OFFSET+SFP_DMT_ADDR,SFP_DMT_WIDTH)
            if cal_type is None:
                return 0
            else:
                cal_type = (int(cal_type[0],16) >> 5 ) & 1
                # read temperature raw value
                temperature_raw = self.read_eeprom_specific_bytes(sysfsfile_eeprom,(DOM_OFFSET+SFP_TEMP_DATA_ADDR),SFP_TEMP_DATA_WIDTH)

        try:
            sysfsfile_eeprom.close()
        except IOError:
            print("Error: closing sysfs file %s" % file_path)
            return 0

        #calculate temperature
        if temperature_raw is not None:
            return self.calc_temperature(cal_type, temperature_raw, 0, 2)
        else:
            return 0