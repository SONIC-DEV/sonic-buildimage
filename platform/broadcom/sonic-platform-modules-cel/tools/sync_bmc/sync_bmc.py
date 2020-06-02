#!/usr/bin/env python

#############################################################################
#                                                                           #
# Platform and model specific service for send data to BMC                  #
#                                                                           #
#                                                                           #
#############################################################################

import subprocess
import requests
import os
import imp
import multiprocessing.pool
import threading


PLATFORM_ROOT_PATH = '/usr/share/sonic/device'
SONIC_CFGGEN_PATH = '/usr/local/bin/sonic-cfggen'
HWSKU_KEY = 'DEVICE_METADATA.localhost.hwsku'
PLATFORM_KEY = 'DEVICE_METADATA.localhost.platform'
TEMP_URL = 'http://240.1.1.1:8080/api/sys/temp'

PLATFORM_SPECIFIC_SFP_MODULE_NAME = "sfputil"
PLATFORM_SPECIFIC_SFP_CLASS_NAME = "SfpUtil"

PLATFORM_SPECIFIC_OPTICTEMP_MODULE_NAME = "optictemputil"
PLATFORM_SPECIFIC_OPTICTEMP_CLASS_NAME = "OpticTempUtil"

PLATFORM_SPECIFIC_CPUTEMP_MODULE_NAME = "cputemputil"
PLATFORM_SPECIFIC_CPUTEMP_CLASS_NAME = "CpuTempUtil"

platform_sfputil = None
platform_optictemputil = None
platform_cputemputil = None


# Returns platform and HW SKU
def get_platform_and_hwsku():
    try:
        proc = subprocess.Popen([SONIC_CFGGEN_PATH, '-H', '-v', PLATFORM_KEY],
                                stdout=subprocess.PIPE,
                                shell=False,
                                stderr=subprocess.STDOUT)
        stdout = proc.communicate()[0]
        proc.wait()
        platform = stdout.rstrip('\n')

        proc = subprocess.Popen([SONIC_CFGGEN_PATH, '-d', '-v', HWSKU_KEY],
                                stdout=subprocess.PIPE,
                                shell=False,
                                stderr=subprocess.STDOUT)
        stdout = proc.communicate()[0]
        proc.wait()
        hwsku = stdout.rstrip('\n')
    except OSError, e:
        raise OSError("Cannot detect platform")

    return (platform, hwsku)


# Returns path to port config file
def get_path_to_port_config_file():
    # Get platform and hwsku
    (platform, hwsku) = get_platform_and_hwsku()

    # Load platform module from source
    platform_path = "/".join([PLATFORM_ROOT_PATH, platform])
    hwsku_path = "/".join([platform_path, hwsku])

    # First check for the presence of the new 'port_config.ini' file
    port_config_file_path = "/".join([hwsku_path, "port_config.ini"])
    if not os.path.isfile(port_config_file_path):
        # port_config.ini doesn't exist. Try loading the legacy 'portmap.ini' file
        port_config_file_path = "/".join([hwsku_path, "portmap.ini"])

    return port_config_file_path


def load_platform_util(module_name, class_name):

    # Get platform and hwsku
    (platform, hwsku) = get_platform_and_hwsku()

    # Load platform module from source
    platform_path = "/".join([PLATFORM_ROOT_PATH, platform])
    hwsku_path = "/".join([platform_path, hwsku])

    try:
        module_file = "/".join([platform_path, "plugins", module_name + ".py"])
        module = imp.load_source(module_name, module_file)
    except IOError, e:
        print("Failed to load platform module '%s': %s" % (
            module_name, str(e)), True)
        return -1

    try:
        platform_util_class = getattr(module, class_name)
        platform_util = platform_util_class()
    except AttributeError, e:
        print("Failed to instantiate '%s' class: %s" %
              (class_name, str(e)), True)
        return -2

    return platform_util


def get_optic_temp(port_list):
    temp_list = []
    for idx, port_eeprom in enumerate(port_list[0]):
        temp = platform_optictemputil.get_optic_temp(
            port_eeprom, port_list[1][idx]) if port_list[2][idx] else 0
        temp_list.append(round(float(temp), 2))
    return max(temp_list)


def get_max_optic_temp():
    port_config_file_path = get_path_to_port_config_file()
    platform_sfputil.read_porttab_mappings(port_config_file_path)
    port_list = platform_sfputil.port_to_i2cbus_mapping
    port_eeprom_list = platform_sfputil.port_to_eeprom_mapping
    qsfp_port_list = platform_sfputil.qsfp_ports

    port_data_list = []
    temp_list = [0]
    i2c_block_size = 32
    concurrent = 10

    port_bus_list = []
    port_type_list = []
    port_presence_list = []

    for port_num, bus_num in port_list.items():
        port_type = "QSFP" if port_num in qsfp_port_list else "SFP"
        port_bus_list.append(port_eeprom_list[port_num])
        port_type_list.append(port_type)
        status = platform_sfputil.get_presence(port_num)
        port_presence_list.append(status)
        if len(port_bus_list) >= i2c_block_size:
            port_tub = (port_bus_list, port_type_list, port_presence_list)
            port_data_list.append(port_tub)
            port_bus_list = []
            port_type_list = []
            port_presence_list = []

    if port_bus_list != []:
        port_tub = (port_bus_list, port_type_list, port_presence_list)
        port_data_list.append(port_tub)

    pool = multiprocessing.pool.ThreadPool(processes=concurrent)
    temp_list = pool.map(get_optic_temp, port_data_list, chunksize=1)
    pool.close()
    return max(temp_list)


# Send CPU temperature to BMC.
def send_cpu_temp():
    max_cpu_tmp = platform_cputemputil.get_max_cpu_tmp()
    json_input = {
        "chip": "cpu",
        "option": "input",
        "value": str(int(max_cpu_tmp))
    }
    print "send ", json_input
    requests.post(TEMP_URL, json=json_input)


# Send maximum optic module temperature to BMC.
def send_optic_temp():
    max_optic_temp = get_max_optic_temp()
    json_input = {
        "chip": "optical",
        "option": "input",
        "value": str(int(max_optic_temp))
    }
    print "send ", json_input
    requests.post(TEMP_URL, json=json_input)


def main():
    global platform_sfputil
    global platform_cputemputil
    global platform_optictemputil

    try:
        platform_sfputil = load_platform_util(
            PLATFORM_SPECIFIC_SFP_MODULE_NAME, PLATFORM_SPECIFIC_SFP_CLASS_NAME)
        platform_cputemputil = load_platform_util(
            PLATFORM_SPECIFIC_CPUTEMP_MODULE_NAME, PLATFORM_SPECIFIC_CPUTEMP_CLASS_NAME)
        platform_optictemputil = load_platform_util(
            PLATFORM_SPECIFIC_OPTICTEMP_MODULE_NAME, PLATFORM_SPECIFIC_OPTICTEMP_CLASS_NAME)

        t1 = threading.Thread(target=send_cpu_temp)
        t2 = threading.Thread(target=send_optic_temp)
        t1.start()
        t2.start()
    except Exception, e:
        print e
        pass


if __name__ == "__main__":
    main()
