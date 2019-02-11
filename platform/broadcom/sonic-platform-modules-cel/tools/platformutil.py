#!/usr/bin/env python
#
# platformutil/main.py
#
# Command-line utility for interacting with PSU in SONiC
#
# example output
# platformutil psu status
# PSU     Presence        Status      PN                  SN
# PSU 1   PRESENT         OK          CSU550AP-3-300      M623TW004ZAAL
# PSU 2   NOT_PRESENT     N/A         N/A                 N/A
#
# platformutil fan status
# FAN     Status          Speed           Low_thd             High_thd        PN              SN
# FAN 1   OK              10169 RPM       300 RPM             16000 RPM       M6510-FAN-F     1000000000014
# FAN 2   NOT_OK          20000 RPM       300 RPM             16000 RPM       M6510-FAN-F     1000000000014
#
# platformutil sensor status
#Sensor             InputName            State    Value     Low_thd    High_thd
#-----------------  -------------------  -------  --------  ---------  ----------
#syscpld-i2c-0-0d   CPU temp             NOT_OK   41.0 C    0 C        0.0 C
#syscpld-i2c-0-0d   Optical temp         NOT_OK   26.0 C    0 C        0.0 C
#syscpld-i2c-0-0d   Switch temp          NOT_OK   35.0 C    0 C        0.0 C
#
# should implenmet the below classes in the specified plugin
#
# class PsuUtil:
#     int get_num_psus();                 //get the number of power supply units
#     bool get_psu_presence(int index)    //get the power status of the psu, index:1,2
#     bool get_psu_status(int index)      //get the running status of the psu,index:1,2
#     str get_psu_sn(int index)       //get the serial number of the psu, return value example: "M623TW004ZAAL"
#     str get_psu_pn(int index)       //get the product name of the psu, return value example: "CSU550AP-3-300"
#
#     // Get all information of PSUs, returns JSON objects in python 'DICT'.
#     // return value of get_all():
#     //     Number: mandatory, max number of PSU, integer
#     //     PSU1, PSU2, ...: mandatory, PSU name, string
#     //     Present: mandatory for each PSU, present status, boolean, True for present, False for NOT present
#     //     PowerStatus: conditional, if PRESENT is True, power status of PSU,
#     //                  boolean, True for powered, False for NOT powered
#     //     PN, conditional, if PRESENT is True, PN of the PSU, string
#     //     SN, conditional, if PRESENT is True, SN of the PSU, string
#     //     example:
#     //     {
#     //         "Number": 2,
#     //         "PSU1": {
#     //             "Present": True,
#     //             "PowerStatus": True,
#     //             "PN": "PN-EXAMPLE-123",
#     //             "SN": "SN-EXAMPLE-123"
#     //         },
#     //         "PSU2": {
#     //             "Present": False
#     //         }
#     //     }
#     dict get_all()

# class FanUtil:
#     int get_fans_name_list();     //get the names of all the fans(FAN1-1,FAN1-2,FAN2-1,FAN2-2...)
#     int get_fan_speed(int index);   //get the current speed of the fan, the unit is "RPM"
#     int get_fan_low_threshold(int index); //get the low speed threshold of the fan, if the current speed < low speed threshold, the status of the fan is ok.
#     int get_fan_high_threshold(int index); //get the hight speed threshold of the fan, if the current speed > high speed threshold, the status of the fan is not ok
#     str get_fan_pn(int index);//get the product name of the fan
#     str get_fan_sn(int index);//get the serial number of the fan
#     // Get all information of system FANs, returns JSON objects in python 'DICT'.
#     // Number, mandatory, max number of FAN, integer
#     // FAN1_1, FAN1_2, ... mandatory, FAN name, string
#     // Present, mandatory for each FAN, present status, boolean, True for present, False for NOT present, read directly from h/w
#     // Running, conditional, if PRESENT is True, running status of the FAN, True for running, False for stopped, read directly from h/w
#     // Speed, conditional, if PRESENT is True, real FAN speed, float, read directly from h/w
#     // LowThd, conditional, if PRESENT is True, lower bound of FAN speed, float, read from h/w
#     // HighThd, conditional, if PRESENT is True, upper bound of FAN speed, float, read from h/w
#     // PN, conditional, if PRESENT is True, PN of the FAN, string
#     // SN, conditional, if PRESENT is True, SN of the FAN, string
#     // Return value python 'dict' object example:
#     // {
#     //     "Number": 3,
#     //     "FAN1_1": {
#     //         "Present": True,
#     //         "Running": True,
#     //         "Speed": 2000.0,
#     //         "LowThd": 1000.0,
#     //         "HighThd": 15000.0,
#     //         "PN": "PN-EXAMPLE-123",
#     //         "SN": "SN-EXAMPLE-123"
#     //         "Status": True,
#     //         "AirFlow": "FTOB"
#     //     },
#     //     "FAN1_2": {
#     //         "Present": True,
#     //         "Running": True,
#     //         "Speed": 2500.0,
#     //         "LowThd": 1000.0,
#     //         "HighThd": 15000.0,
#     //         "PN": "PN-EXAMPLE-456",
#     //         "SN": "SN-EXAMPLE-456"
#     //         "Status": True,
#     //         "AirFlow": "BTOF"
#     //     },
#     //     "FAN2_1": {
#     //         "Present": True,
#     //         "Running": False
#     //     },
#     //     "FAN2_2": {
#     //         "Present": True,
#     //         "Running": False
#     //     },
#     //     "FAN3_1": {
#     //         "Present": False
#     //     },
#     //     "FAN3_2": {
#     //         "Present": False
#     //     }
#     // }
#     dict get_all()
#
# class SensorUtil:
#     int get_num_sensors();  //get the number of sensors
#     int get_sensor_input_num(int index); //get the number of the input items of the specified sensor
#     str get_sensor_name(int index);// get the device name of the specified sensor.for example "coretemp-isa-0000"
#     str get_sensor_input_name(int sensor_index, int input_index); //get the input item name of the specified input item of the specified sensor index, for example "Physical id 0"
#     str get_sensor_input_type(int sensor_index, int input_index); //get the item type of the specified input item of the specified sensor index, the return value should
#                                                                   //among  "voltage","temperature"...
#     float get_sensor_input_value(int sensor_index, int input_index);//get the current value of the input item, the unit is "V" or "C"...
#     float get_sensor_input_low_threshold(int sensor_index, int input_index); //get the low threshold of the value, the status of this item is not ok if the current
#                                                                                 //value<low_threshold
#     float get_sensor_input_high_threshold(int sensor_index, int input_index); //get the high threshold of the value, the status of this item is not ok if the current
#                                                                                   // value > high_threshold
#     // Get all information of system sensors, returns JSON objects in python 'DICT'.
#     // SensorName1, SensorName2, ... optional, string
#     // SensorInput1, SensorInput2, ... optional, string
#     // Type, mandatory in SensorInput$INDEX, should be on of { "temperature", "voltage", "power", "amp", "RPM" }
#     // Value, mandatory in SensorInput$INDEX, float , real value
#     // LowThd, mandatory in SensorInput$INDEX, float , lower bound of value
#     // HighThd, mandatory in SensorInput$INDEX, float , upper bound of value
#     // Return python 'dict' objects, example:
#     // {
#     //     "SensorName1": {
#     //         "CPU_TEMP":
#     //             "Type": "temperature",
#     //             "Value": 37.3,
#     //             "LowThd": 0.0,
#     //             "HighThd": 110.0
#     //         },
#     //         "SWITCH_TEMP": {
#     //             "Type": "temperature",
#     //             "Value": 45.2,
#     //             "LowThd": 0.0,
#     //             "HighThd": 108.0
#     //         },
#     //         "INLET_TEMP": {
#     //             "Type": "temperature",
#     //             "Value": 22.0,
#     //             "LowThd": 0.0,
#     //             "HighThd": 70.0
#     //         },
#     //         "Sys_AirFlow": "BTOF",
#     //         "Switch_VDDCore_0.8v": {
#     //             "Type": "voltage",
#     //             "Value": 0.75,
#     //             "LowThd": 0.7,
#     //             "HighThd": 0.85
#     //         },
#     //         "Cpu_VDDCore_0.8v": {
#     //             "Type": "voltage",
#     //             "Value": 0.75,
#     //             "LowThd": 0.7,
#     //             "HighThd": 0.85
#     //         },
#     //         "SensorInput1": {
#     //             "Type": "temperature",
#     //             "Value": 30.0,
#     //             "LowThd": 0.0,
#     //             "HighThd": 100.0"
#     //         },
#     //         "SensorInput2": {
#     //             "Type": "voltage",
#     //             "Value": 0.5,
#     //             "LowThd": 0.0,
#     //             "HighThd": 1.5
#     //         },
#     //         "SensorInput3": {
#     //             "Type": "power",
#     //             "Value": 2.5,
#     //             "LowThd": 0.0,
#     //             "HighThd": 5.0
#     //         }
#     //     },
#     //     "SensorName2": {
#     //         "SensorInput1": {
#     //             "Type": "RPM",
#     //             "Value": 2000.0,
#     //             "LowThd": 1000.0,
#     //             "HighThd": 15000.0
#     //         },
#     //         "SensorInputName2": {
#     //             "Type": "amp",
#     //             "Value": 0.1,
#     //             "LowThd": 0.0,
#     //             "HighThd": 0.3
#     //         }
#     //     }
#     // }

try:
    import sys
    import os
    import subprocess
    import click
    import imp
    import syslog
    import types
    import traceback
    from tabulate import tabulate
except ImportError as e:
    raise ImportError("%s - required module not found" % str(e))

VERSION = '1.2'

SYSLOG_IDENTIFIER = "platformutil"
PLATFORM_PSU_MODULE_NAME = "psuutil"
PLATFORM_PSU_CLASS_NAME = "PsuUtil"

#gongjian add
PLATFORM_SENSOR_MODULE_NAME = "sensorutil"
PLATFORM_SENSOR_CLASS_NAME = "SensorUtil"

PLATFORM_FAN_MODULE_NAME = "fanutil"
PLATFORM_FAN_CLASS_NAME = "FanUtil"
#end gongjian add

PLATFORM_ROOT_PATH = '/usr/share/sonic/device'
PLATFORM_ROOT_PATH_DOCKER = '/usr/share/sonic/platform'
SONIC_CFGGEN_PATH = '/usr/local/bin/sonic-cfggen'
MINIGRAPH_PATH = '/etc/sonic/minigraph.xml'
HWSKU_KEY = "DEVICE_METADATA['localhost']['hwsku']"
PLATFORM_KEY = "DEVICE_METADATA['localhost']['platform']"

# Global platform-specific psuutil class instance
platform_psuutil = None

#gongjian add
platform_sensorutil = None
Platform_fanutil = None
#end gongjian add

# ========================== Syslog wrappers ==========================


def log_info(msg, also_print_to_console=False):
    syslog.openlog(SYSLOG_IDENTIFIER)
    syslog.syslog(syslog.LOG_INFO, msg)
    syslog.closelog()

    if also_print_to_console:
        click.echo(msg)


def log_warning(msg, also_print_to_console=False):
    syslog.openlog(SYSLOG_IDENTIFIER)
    syslog.syslog(syslog.LOG_WARNING, msg)
    syslog.closelog()

    if also_print_to_console:
        click.echo(msg)


def log_error(msg, also_print_to_console=False):
    syslog.openlog(SYSLOG_IDENTIFIER)
    syslog.syslog(syslog.LOG_ERR, msg)
    syslog.closelog()

    if also_print_to_console:
        click.echo(msg)


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


# Loads platform specific psuutil module from source
def load_platform_util():
    global platform_psuutil
    #gongjian add
    global platform_sensorutil
    global platform_fanutil

    # Get platform and hwsku
    (platform, hwsku) = get_platform_and_hwsku()

    # Load platform module from source
    platform_path = ''
    if len(platform) != 0:
        platform_path = "/".join([PLATFORM_ROOT_PATH, platform])
    else:
        platform_path = PLATFORM_ROOT_PATH_DOCKER
    hwsku_path = "/".join([platform_path, hwsku])

    try:
        module_file_psu = "/".join([platform_path, "plugins", PLATFORM_PSU_MODULE_NAME + ".py"])
        module_psu = imp.load_source(PLATFORM_PSU_MODULE_NAME, module_file_psu)
    except IOError, e:
        log_error("Failed to load platform module '%s': %s" % (PLATFORM_PSU_MODULE_NAME, str(e)), True)
        return -1

    try:
        platform_psuutil_class = getattr(module_psu, PLATFORM_PSU_CLASS_NAME)
        platform_psuutil = platform_psuutil_class()
    except AttributeError, e:
        log_error("Failed to instantiate '%s' class: %s" % (PLATFORM_PSU_CLASS_NAME, str(e)), True)
        return -2


    #gongjian add
    try:
        module_file_sensor = "/".join([platform_path, "plugins", PLATFORM_SENSOR_MODULE_NAME + ".py"])
        module_sensor = imp.load_source(PLATFORM_SENSOR_MODULE_NAME, module_file_sensor)
    except IOError, e:
        log_error("Failed to load platform module '%s': %s" % (PLATFORM_SENSOR_MODULE_NAME, str(e)), True)
        return -1

    try:
        platform_sensorutil_class = getattr(module_sensor, PLATFORM_SENSOR_CLASS_NAME)
        platform_sensorutil = platform_sensorutil_class()
    except AttributeError, e:
        log_error("Failed to instantiate '%s' class: %s" % (PLATFORM_SENSOR_CLASS_NAME, str(e)), True)
        return -2

    try:
        module_file_fan = "/".join([platform_path, "plugins", PLATFORM_FAN_MODULE_NAME + ".py"])
        module_fan = imp.load_source(PLATFORM_FAN_MODULE_NAME, module_file_fan)
    except IOError, e:
        log_error("Failed to load platform module '%s': %s" % (PLATFORM_FAN_MODULE_NAME, str(e)), True)
        return -1

    try:
        platform_fanutil_class = getattr(module_fan, PLATFORM_FAN_CLASS_NAME)
        platform_fanutil = platform_fanutil_class()
    except AttributeError, e:
        log_error("Failed to instantiate '%s' class: %s" % (PLATFORM_FAN_CLASS_NAME, str(e)), True)
        return -2
    #end gongjian add
    return 0


# ==================== CLI commands and groups ====================


# This is our main entrypoint - the main 'psuutil' command
@click.group()
def cli():
    """platformutil - Command line utility for providing platform status"""

    if os.geteuid() != 0:
        click.echo("Root privileges are required for this operation")
        sys.exit(1)

    # Load platform-specific psuutil, fanutil and sensorutil class
    err = load_platform_util()
    if err != 0:
        sys.exit(2)

#'fan' subcommand
@cli.group()
@click.pass_context
def fan(ctx):
    """fan state"""
    ctx.obj = "fan"

# 'sensor' subcommand
@cli.group()
@click.pass_context
def sensor(ctx):
    """sensor state"""
    ctx.obj = "sensor"

# 'psu' subcommand
@cli.group()
@click.pass_context
def psu(ctx):
    """psu state"""
    ctx.obj = "psu"

# 'version' subcommand
@cli.command()
def version():
    """Display version info"""
    click.echo("platformutil version {0}".format(VERSION))


# 'num' subcommand
@click.command()
@click.pass_context
def num(ctx):
    """Display number of supported sensor/fan/psu device"""
    if ctx.obj == "psu":
        click.echo(str(platform_psuutil.get_num_psus()))
    if ctx.obj == "fan":
        click.echo(str(len(platform_fanutil.get_fans_name_list())))
    if ctx.obj == "sensor":
        click.echo(str(platform_sensorutil.get_num_sensors()))

psu.add_command(num)
sensor.add_command(num)
fan.add_command(num)

# 'status' subcommand
#all API should return "N/A" or float("-intf") if not supported
@click.command()
@click.pass_context
def status(ctx):
    if ctx.obj == 'psu':
        psu_dict = platform_psuutil.get_all()
        if psu_dict == None:
            print 'Error: psuutil.get_all() failed'
            return

        psu_nr = psu_dict.get('Number')
        if psu_nr == None:
            print 'Error: PSU get all format invalid, prop "Number" missing.'
            return

        psu_names = [ k for k in psu_dict.keys() if cmp('Number', k) != 0 ]
        psu_names.sort()
        header = ['PSU', 'Presence', 'Status', 'PN', 'SN']
        status_table = []
        for psu_name in psu_names:
            psu = psu_dict[psu_name]
            presence = psu.get('Present')
            pwr_status = psu.get('PowerStatus')
            pn = psu.get('PN')
            sn = psu.get('SN')
            if presence == None:
                print 'Error: PSU get all format invaid, prop "Present" missing.'
                continue
            elif presence == False:
                presence = 'NOT_PRESENT'
                pwr_status = 'N/A'
                pn = 'N/A'
                sn = 'N/A'
            else:
                presence = 'PRESENT'
                if pwr_status == None:
                    pwr_status = 'N/A'
                elif pwr_status == True:
                    pwr_status = 'OK'
                else:
                    pwr_status = 'NOT_OK'
                if pn == None:
                    pn = 'N/A'
                if sn == None:
                    sn = 'N/A'
            status_table.append([psu_name, presence, pwr_status, pn, sn])

        if len(status_table) != psu_nr:
            print 'Error: PSU get all missing some PSU information.'

        if len(status_table) > 0:
            click.echo(tabulate(status_table, header, tablefmt='simple'))

    if ctx.obj == 'fan':
        fan_dict = platform_fanutil.get_all()
        if fan_dict == None:
            print 'Error: fanutil.get_all() failed'
            return

        fan_nr = fan_dict.get('Number')
        if fan_nr == None:
            print 'Error: FAN get all format invalid, prop "Number" missing.'
            return

        header = [ 'FAN', 'Presence', 'Running', 'Speed', 'LowThd', 'HighThd', 'PN', 'SN' ]
        status_table = []
        fan_names = [ k for k in fan_dict.keys() if cmp('Number', k) != 0 ]
        fan_names.sort()
        for fan_name in fan_names:
            fan = fan_dict[fan_name]
            presence = fan.get('Present')
            running = fan.get('Running')
            speed = fan.get('Speed')
            low = fan.get('LowThd')
            high = fan.get('HighThd')
            pn = fan.get('PN')
            sn = fan.get('SN')

            if presence == None:
                print 'Error: FAN get all format invaid, prop "Present" missing.'
                continue
            elif presence == False:
                presence = 'NOT_PRESENT'
                running = 'N/A'
                speed = 'N/A'
                low = 'N/A'
                high = 'N/A'
                pn = 'N/A'
                sn = 'N/A'
            else:
                presence = 'PRESENT'
                if running == None:
                    print 'Error: FAN get all format invalid, prop "Running" missing.'
                    running = 'N/A'
                elif running == True:
                    running = 'YES'
                else:
                    running = 'NO'
                if speed == None:
                    speed = 'N/A'
                if low == None:
                    low = 'N/A'
                if high == None:
                    high = 'N/A'
                if pn == None:
                    pn = 'N/A'
                if sn == None:
                    sn = 'N/A'

            status_table.append([fan_name, presence, running, speed, low, high, pn, sn])

        if len(status_table) != fan_nr:
            print 'Error: FAN get all missing some FAN information.'

        if len(status_table) > 0:
            click.echo(tabulate(status_table, header, tablefmt='simple'))

    if ctx.obj == 'sensor':
        sensor_dict = platform_sensorutil.get_all()
        if sensor_dict == None:
            print 'Error: sensors.get_all() failed'
            return

        header = [ 'Sensor', 'InputName', 'State', 'Value', 'LowThd', 'HighThd' ]
        status_table = []
        type2unit = { 'temperature' : ' C', 'voltage' : ' V', 'RPM' : ' RPM', 'amp' : ' A', 'power' : ' W'}
        type_keys = type2unit.keys()
        for sensor_name, sensor_obj in sensor_dict.items():
            if cmp(sensor_name, 'Number') == 0:
                continue

            si_names = [ k for k in sensor_obj.keys() ]
            si_names.sort()
            for si_name in si_names:
                if si_name == "Sys_AirFlow":
                    continue
                si = sensor_obj[si_name]
                stype = si.get('Type')
                sval = si.get('Value')
                slow = si.get('LowThd')
                shigh = si.get('HighThd')
                sunit = ' '
                fault = False
                if stype != None:
                    sunit = type2unit.get(stype)
                    if sunit == None:
                        sunit = ' '
                try:
                    sval = float(sval)
                except:
                    sval = 0.0
                    fault = True

                try:
                    slow = float(slow)
                except:
                    slow = 0.0
                    fault = True

                try:
                    shigh = float(shigh)
                except:
                    shigh = 0.0
                    fault = True

                status = 'NOT_OK'
                if fault == False and sval > slow and sval < shigh:
                    status = 'OK'

                status_table.append([sensor_name, si_name, status, (str(sval)+sunit), (str(slow)+sunit), (str(shigh)+sunit)])

        if len(status_table) > 0:
            click.echo(tabulate(status_table, header, tablefmt="simple"))

    return

psu.add_command(status)
sensor.add_command(status)
fan.add_command(status)


if __name__ == '__main__':
    cli()
