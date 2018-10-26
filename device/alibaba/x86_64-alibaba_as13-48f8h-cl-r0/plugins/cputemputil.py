#!/usr/bin/env python

#
#  cputemputil.py
#
#  Platform-specific CPU temperature Interface for SONiC
#

__author__ = 'Wirut G.<wgetbumr@celestica.com>'
__license__ = "GPL"
__version__ = "0.1.0"
__status__  = "Development"

import subprocess
import requests

class CpuTempUtil():
    """Platform-specific CpuTempUtil class"""

    def __init__(self):
        self.temp_url = "http://[fe80::1:1%eth0.4088]:8080/api/sys/temp"


    def get_cpu_temp(self):

        # Get list of temperature of CPU cores.
        p = subprocess.Popen(['sensors', '-Au', 'coretemp-isa-0000'], stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        out, err = p.communicate()
        raw_data_list = out.splitlines()
        temp_string_list = [i for i, s in enumerate(raw_data_list) if '_input' in s]
        tmp_list = [0]
        
        for temp_string in temp_string_list:
            tmp_list.append(float(raw_data_list[temp_string].split(":")[1]))
        
        return tmp_list


    def get_max_cpu_tmp(self):
        # Get maximum temperature from list of temperature of CPU cores.
        return max(self.get_cpu_temp())
