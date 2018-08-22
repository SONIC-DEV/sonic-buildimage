#!/usr/bin/env python

import os.path
import subprocess
import sys
import re

try:
    from sonic_psu.psu_base import PsuBase
except ImportError as e:
    raise ImportError(str(e) + "- required module not found")


class PsuUtil(PsuBase):
    """Platform-specific PSUutil class"""

    def __init__(self):
        PsuBase.__init__(self)

    def run_command(self, command):
        proc = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE)
        (out, err) = proc.communicate()

        if proc.returncode != 0:
            sys.exit(proc.returncode)

        return out

    def find_value(self, grep_string):
        return re.findall("[-+]?\d*\.\d+|\d+", grep_string)

    def get_num_psus(self):
        """
        Retrieves the number of PSUs available on the device
        :return: An integer, the number of PSUs available on the device
        """
        return 2

    def get_psu_status(self, index):
        """
        Retrieves the oprational status of power supply unit (PSU) defined
                by 1-based index <index>
        :param index: An integer, 1-based index of the PSU of which to query status
        :return: Boolean, True if PSU is operating properly, False if PSU is faulty
        """
        if index is None:
            return False

        ''' L index = 1 , R index = 2 
            Power status is active hight
        '''
        res = self.run_command('ipmitool raw 0x3a 0x0c 0x00 0x02 0x60')
        status = int(res,16) & 0x3
        if index == 1:
            return (status >> 1) & 1
        else:
            return status & 1

    def get_psu_presence(self, index):
        """
        Retrieves the presence status of power supply unit (PSU) defined
                by 1-based index <index>
        :param index: An integer, 1-based index of the PSU of which to query status
        :return: Boolean, True if PSU is plugged, False if not
        """
        if index is None:
            return False

        ''' L index = 1 , R index = 2 
            Presence status is active Low
        '''
        res = self.run_command('ipmitool raw 0x3a 0x0c 0x00 0x02 0x60')
        status = int(res,16) & 0xc
        if index == 1:
            return not((status >> 3) & 1)
        else:
            return not((status >> 2) & 1)
