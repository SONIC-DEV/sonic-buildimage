# fwmgrutil.py
#
# Platform-specific firmware management interface for SONiC
#

import subprocess
import requests

try:
    from sonic_fwmgr.fwgmr_base import FwMgrUtilBase
except ImportError as e:
    raise ImportError("%s - required module not found" % str(e))


class FwMgrUtil(FwMgrUtilBase):

    """Platform-specific FwMgrUtil class"""

    def __init__(self):
        self.platform_name = "AS1348f8h"
        self.onie_config_file = "/host/machine.conf"
        self.bmc_info_url = "http://240.1.1.1:8080/api/sys/bmc"
        self.onie_config_file = "/host/machine.conf"
        self.cpldb_version_path = "/sys/devices/platform/%s.cpldb/getreg" % self.platform_name
        self.fpga_version_path = "/sys/devices/platform/%s.switchboard/FPGA/getreg" % self.platform_name
        self.switchboard_cpld1_path = "/sys/devices/platform/%s.switchboard/CPLD1/getreg" % self.platform_name
        self.switchboard_cpld2_path = "/sys/devices/platform/%s.switchboard/CPLD2/getreg" % self.platform_name
        self.switchboard_cpld3_path = "/sys/devices/platform/%s.switchboard/CPLD3/getreg" % self.platform_name
        self.switchboard_cpld4_path = "/sys/devices/platform/%s.switchboard/CPLD4/getreg" % self.platform_name

    def __get_register_value(self, path, register):
        cmd = "echo {1} > {0}; cat {0}".format(path, register)
        p = subprocess.Popen(
            cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        raw_data, err = p.communicate()
        if err is not '':
            return 'None'
        else:
            return raw_data.strip()

    def get_bmc_version(self):
        """Get BMC version from SONiC
        :returns: version string

        """
        bmc_version = "None"

        bmc_version_key = "OpenBMC Version"
        bmc_info_req = requests.get(self.bmc_info_url)
        bmc_info_json = bmc_info_req.json()
        bmc_info = bmc_info_json.get('Information')
        bmc_version = bmc_info.get(bmc_version_key)

        return bmc_version

    def get_cpld_version(self):
        """Get CPLD version from SONiC
        :returns: dict like {'CPLD_1': version_string, 'CPLD_2': version_string}
        """

        CPLD_B = self.__get_register_value(self.cpldb_version_path, '0xA100')
        CPLD_C = self.__get_register_value(self.cpldb_version_path, '0xA1E0')
        CPLD_1 = self.__get_register_value(self.switchboard_cpld1_path, '0x00')
        CPLD_2 = self.__get_register_value(self.switchboard_cpld2_path, '0x00')
        CPLD_3 = self.__get_register_value(self.switchboard_cpld3_path, '0x00')
        CPLD_4 = self.__get_register_value(self.switchboard_cpld4_path, '0x00')

        fan_cpld_key = "FanCPLD Version"
        bmc_info_req = requests.get(self.bmc_info_url)
        bmc_info_json = bmc_info_req.json()
        bmc_info = bmc_info_json.get('Information')
        fan_cpld = bmc_info.get(fan_cpld_key)

        CPLD_B = 'None' if CPLD_B is 'None' else "{}.{}".format(
            int(CPLD_B[2], 16), int(CPLD_B[3], 16))
        CPLD_C = 'None' if CPLD_C is 'None' else "{}.{}".format(
            int(CPLD_C[2], 16), int(CPLD_C[3], 16))
        CPLD_1 = 'None' if CPLD_1 is 'None' else "{}.{}".format(
            int(CPLD_1[2], 16), int(CPLD_1[3], 16))
        CPLD_2 = 'None' if CPLD_2 is 'None' else "{}.{}".format(
            int(CPLD_2[2], 16), int(CPLD_2[3], 16))
        CPLD_3 = 'None' if CPLD_3 is 'None' else "{}.{}".format(
            int(CPLD_3[2], 16), int(CPLD_3[3], 16))
        CPLD_4 = 'None' if CPLD_4 is 'None' else "{}.{}".format(
            int(CPLD_4[2], 16), int(CPLD_4[3], 16))
        FAN_CPLD = 'None' if CPLD_4 is None else "{:.1f}".format(float(fan_cpld))

        cpld_version_dict={}
        cpld_version_dict.update({'CPLD_B': CPLD_B})
        cpld_version_dict.update({'CPLD_C': CPLD_C})
        cpld_version_dict.update({'CPLD_1': CPLD_1})
        cpld_version_dict.update({'CPLD_2': CPLD_2})
        cpld_version_dict.update({'CPLD_3': CPLD_3})
        cpld_version_dict.update({'CPLD_4': CPLD_4})
        cpld_version_dict.update({'CPLD_FAN': FAN_CPLD})

        return cpld_version_dict

    def get_bios_version(self):
        """Get BIOS version from SONiC
        :returns: version string

        """
        bios_version='None'

        p=subprocess.Popen(
            ["sudo", "dmidecode", "-s", "bios-version"], stdout=subprocess.PIPE)
        raw_data=str(p.communicate()[0])
        raw_data_list=raw_data.split("\n")
        bios_version=raw_data_list[0] if len(
            raw_data_list) == 1 else raw_data_list[-2]

        return bios_version

    def get_onie_version(self):
        """Get ONiE version from SONiC
        :returns: version string

        """
        onie_verison='None'

        onie_version_keys="onie_version"
        onie_config_file=open(self.onie_config_file, "r")
        for line in onie_config_file.readlines():
            if onie_version_keys in line:
                onie_version_raw=line.split('=')
                onie_verison=onie_version_raw[1].strip()
                break
        return onie_verison

    def get_pcie_version(self):
        """Get PCiE version from SONiC
        :returns: version string
        """
        cmd="sudo bcmcmd 'pciephy fw version'"
        p=subprocess.Popen(
            cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        raw_data, err=p.communicate()
        if err is not '':
            return 'None'
        else:
            lines=raw_data.split('\n')
            for line in lines:
                if 'PCIe FW loader' in line:
                    version=line.split(':')[1].strip()
        return str(version)

    def get_fpga_version(self):
        """Get FPGA version from SONiC
        :returns: version string

        """
        version=self.__get_register_value(self.fpga_version_path, '0x00')
        if version is not 'None':
            version="{}.{}".format(
                int(version[2:][:4], 16), int(version[2:][4:], 16))
        return str(version)
