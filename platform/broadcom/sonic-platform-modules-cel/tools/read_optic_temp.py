#!/usr/bin/env python
#
# read_optic_temp.py
#
# Command-line utility for read the temperature of optic modules.
#

try:
    import sys
    import os
    import subprocess
    import click
    import imp
    import multiprocessing.pool
    import threading
except ImportError as e:
    raise ImportError("%s - required module not found" % str(e))


PLATFORM_ROOT_PATH = '/usr/share/sonic/device'
SONIC_CFGGEN_PATH = '/usr/local/bin/sonic-cfggen'
HWSKU_KEY = 'DEVICE_METADATA.localhost.hwsku'
PLATFORM_KEY = 'DEVICE_METADATA.localhost.platform'

PLATFORM_SPECIFIC_SFP_MODULE_NAME = "sfputil"
PLATFORM_SPECIFIC_SFP_CLASS_NAME = "SfpUtil"

PLATFORM_SPECIFIC_OPTICTEMP_MODULE_NAME = "optictemputil"
PLATFORM_SPECIFIC_OPTICTEMP_CLASS_NAME = "OpticTempUtil"

# Global platform-specific psuutil class instance
platform_optictemputil = None
platform_sfputil = None


# ==================== Methods for initialization ====================

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
        print("Failed to load platform module '%s': %s" % (module_name, str(e)))
        sys.exit(1)

    try:
        platform_util_class = getattr(module, class_name)
        platform_util = platform_util_class()
    except AttributeError, e:
        print("Failed to instantiate '%s' class: %s" % (class_name, str(e)))
        sys.exit(1)

    return platform_util


def get_optic_temp(port_list):
    temp_list = []
    for idx, port_num in enumerate(port_list[0]):
        temp = platform_optictemputil.get_optic_temp(
            port_num, port_list[1][idx])
        temp_list.append(round(float(temp), 2))
    return temp_list


# ========================= CLI commands =========================

# This is our main entrypoint - the main 'opticutil' command
@click.command()
@click.option('--port_num', '-p', type=int, help='Specific port number')
def cli(port_num):
    """optictemputil - Command line utility for providing platform status"""

    # Check root privileges
    if os.geteuid() != 0:
        click.echo("Root privileges are required for this operation")
        sys.exit(1)

    global platform_optictemputil
    global platform_sfputil

    # Load platform-specific class
    platform_sfputil = load_platform_util(
        PLATFORM_SPECIFIC_SFP_MODULE_NAME, PLATFORM_SPECIFIC_SFP_CLASS_NAME)
    platform_optictemputil = load_platform_util(
        PLATFORM_SPECIFIC_OPTICTEMP_MODULE_NAME, PLATFORM_SPECIFIC_OPTICTEMP_CLASS_NAME)

    # Load port config
    port_config_file_path = get_path_to_port_config_file()
    platform_sfputil.read_porttab_mappings(port_config_file_path)
    port_list = platform_sfputil.port_to_i2cbus_mapping
    port_eeprom_list = platform_sfputil.port_to_eeprom_mapping
    qsfp_port_list = platform_sfputil.qsfp_ports

    port_dict = {}
    temp_list = [0]
    i2c_block_size = 32
    concurrent = 10

    port_data_list = []
    port_bus_list = []
    port_type_list = []

    # Read port temperature
    if port_num:
        if port_num not in port_list:
            click.echo("Invalid port")
            sys.exit(1)
        port_list = {port_num: port_list.get(port_num)}

    for port_num, bus_num in port_list.items():
        port_type = "QSFP" if port_num in qsfp_port_list else "SFP"
        port_bus_list.append(port_eeprom_list[port_num])
        port_type_list.append(port_type)
        if len(port_bus_list) >= i2c_block_size:
            port_tub = (port_bus_list, port_type_list)
            port_data_list.append(port_tub)
            port_bus_list = []
            port_type_list = []

    if port_bus_list != []:
        port_tub = (port_bus_list, port_type_list)
        port_data_list.append(port_tub)

    pool = multiprocessing.pool.ThreadPool(processes=concurrent)
    temp_list = pool.map(get_optic_temp, port_data_list, chunksize=1)
    pool.close()

    flat_list = [item for sublist in temp_list for item in sublist]
    click.echo("| PORT_NO\t| PORT_TYPE\t| TEMPERATURE\t|")
    for port_num, bus_num in port_list.items():
        port_type = "QSFP" if port_num in qsfp_port_list else "SFP"
        temp_idx = port_list.keys().index(port_num)
        temp = flat_list[temp_idx]
        click.echo('| {}\t\t| {}\t\t| {}\t\t|'.format(
            port_num, port_type, temp))


if __name__ == '__main__':
    cli()
