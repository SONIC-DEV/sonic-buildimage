# fwmgrutil.py
#
# Platform-specific firmware management interface for SONiC
#

import subprocess
import requests
import os
import pexpect
import base64
import time
from datetime import datetime

try:
    from sonic_fwmgr.fwgmr_base import FwMgrUtilBase
except ImportError as e:
    raise ImportError("%s - required module not found" % str(e))


class FwMgrUtil(FwMgrUtilBase):

    """Platform-specific FwMgrUtil class"""

    def __init__(self):
        self.platform_name = "AS1332h"
        self.onie_config_file = "/host/machine.conf"
        self.bmc_info_url = "http://240.1.1.1:8080/api/sys/bmc"
        self.bmc_raw_command_url = "http://240.1.1.1:8080/api/sys/raw"
        self.fw_upgrade_url = "http://240.1.1.1:8080/api/sys/upgrade"
        self.onie_config_file = "/host/machine.conf"
        self.cpldb_version_path = "/sys/devices/platform/%s.cpldb/getreg" % self.platform_name
        self.fpga_version_path = "/sys/devices/platform/%s.switchboard/FPGA/getreg" % self.platform_name
        self.switchboard_cpld1_path = "/sys/devices/platform/%s.switchboard/CPLD1/getreg" % self.platform_name
        self.switchboard_cpld2_path = "/sys/devices/platform/%s.switchboard/CPLD2/getreg" % self.platform_name
        self.switchboard_cpld3_path = "/sys/devices/platform/%s.switchboard/CPLD3/getreg" % self.platform_name
        self.switchboard_cpld4_path = "/sys/devices/platform/%s.switchboard/CPLD4/getreg" % self.platform_name
        self.bmc_pwd_path = "/usr/local/etc/bmcpwd"

    def __get_register_value(self, path, register):
        cmd = "echo {1} > {0}; cat {0}".format(path, register)
        p = subprocess.Popen(
            cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        raw_data, err = p.communicate()
        if err is not '':
            return 'None'
        else:
            return raw_data.strip()

    def __fpga_pci_rescan(self):
        """
        An sequence to trigger FPGA to load new configuration after upgrade.
        """
        fpga_pci_device_remove = '/sys/devices/pci0000:00/0000:00:1c.0/0000:09:00.0/remove'
        parent_pci_device_rescan = '/sys/devices/pci0000:00/0000:00:1c.0/rescan'
        cmd = 'modprobe -r switchboard_fpga'
        os.system(cmd)
        cmd = 'echo 1 > %s' % fpga_pci_device_remove
        rc = os.system(cmd)
        if rc > 0:
            return rc
        cmd = 'echo 0xa10a 0 > /sys/devices/platform/%s.cpldb/setreg' % self.platform_name
        rc = os.system(cmd)
        if rc > 0:
            return rc
        time.sleep(10)
        cmd = 'echo 1 > %s' % parent_pci_device_rescan
        rc = os.system(cmd)
        if rc > 0:
            return rc
        os.system('modprobe switchboard_fpga')
        return 0

    def get_bmc_pass(self):
        with open(self.bmc_pwd_path) as file:
            data = file.read()

        key = "bmc"
        dec = []
        enc = base64.urlsafe_b64decode(data)
        for i in range(len(enc)):
            key_c = key[i % len(key)]
            dec_c = chr((256 + ord(enc[i]) - ord(key_c)) % 256)
            dec.append(dec_c)
        return "".join(dec)

    def get_bmc_version(self):
        """Get BMC version from SONiC
        :returns: version string

        """
        bmc_version = None

        bmc_version_key = "OpenBMC Version"
        bmc_info_req = requests.get(self.bmc_info_url)
        if bmc_info_req.status_code == 200:
            bmc_info_json = bmc_info_req.json()
            bmc_info = bmc_info_json.get('Information')
            bmc_version = bmc_info.get(bmc_version_key)
        return str(bmc_version)

    def upload_file_bmc(self, fw_path):
        scp_command = 'sudo scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -r %s root@240.1.1.1:/home/root/' % os.path.abspath(
            fw_path)
        child = pexpect.spawn(scp_command)
        i = child.expect(["root@240.1.1.1's password:"], timeout=30)
        if i == 0:
            bmc_pwd = self.get_bmc_pass()
            child.sendline(bmc_pwd)
            data = child.read()
            print(data)
            child.close
            return True
        return False

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
        fan_cpld = None
        bmc_info_req = requests.get(self.bmc_info_url)
        if bmc_info_req.status_code == 200:
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
        FAN_CPLD = 'None' if fan_cpld is None else "{}.{}".format(
            int(fan_cpld[0], 16), int(fan_cpld[1], 16))

        cpld_version_dict = {}
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
        bios_version = None

        p = subprocess.Popen(
            ["sudo", "dmidecode", "-s", "bios-version"], stdout=subprocess.PIPE)
        raw_data = str(p.communicate()[0])
        if raw_data == '':
            return str(None)
        raw_data_list = raw_data.split("\n")
        bios_version = raw_data_list[0] if len(
            raw_data_list) == 1 else raw_data_list[-2]

        return str(bios_version)

    def get_onie_version(self):
        """Get ONiE version from SONiC
        :returns: version string

        """
        onie_verison = None

        onie_version_keys = "onie_version"
        onie_config_file = open(self.onie_config_file, "r")
        for line in onie_config_file.readlines():
            if onie_version_keys in line:
                onie_version_raw = line.split('=')
                onie_verison = onie_version_raw[1].strip()
                break
        onie_config_file.close()
        return str(onie_verison)

    def get_pcie_version(self):
        """Get PCiE version from SONiC
        :returns: version dict { "PCIE_FW_LOADER": "2.5", "PCIE_FW": "D102_08" }

        """
        cmd = "sudo bcmcmd 'pciephy fw version'"
        p = subprocess.Popen(
            cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        raw_data, err = p.communicate()

        pcie_version = dict()
        pcie_version["PCIE_FW_LOADER"] = 'None'
        pcie_version["PCIE_FW"] = 'None'

        if err == '':
            lines = raw_data.split('\n')
            for line in lines:
                if 'PCIe FW loader' in line:
                    pcie_version["PCIE_FW_LOADER"] = line.split(':')[1].strip()
                elif 'PCIe FW version' in line:
                    pcie_version["PCIE_FW"] = line.split(':')[1].strip()
        return pcie_version

    def get_fpga_version(self):
        """Get FPGA version from SONiC
        :returns: version string

        """
        version = self.__get_register_value(self.fpga_version_path, '0x00')
        if version is not 'None':
            version = "{}.{}".format(
                int(version[2:][:4], 16), int(version[2:][4:], 16))
        return str(version)

    def firmware_upgrade(self, fw_type, fw_path, fw_extra=None):
        """
            @fw_type MANDATORY, firmware type, should be one of the strings: 'cpld', 'fpga', 'bios', 'bmc'
            @fw_path MANDATORY, target firmware file
            @fw_extra OPTIONAL, extra information string,

            for fw_type 'cpld' and 'fpga': it can be used to indicate specific cpld, such as 'cpld1', 'cpld2', ...
                or 'cpld_fan_come_board', etc. If None, upgrade all CPLD/FPGA firmware. for fw_type 'bios' and 'bmc',
                 value should be one of 'master' or 'slave' or 'both'
        """
        bmc_pwd = self.get_bmc_pass()
        if fw_type == 'bmc':
            # Copy BMC image file to BMC
            print("Uploading image to BMC...")
            upload_file = self.upload_file_bmc(fw_path)
            if not upload_file:
                print("Failed")
                return False

            filename_w_ext = os.path.basename(fw_path)
            json_data = dict()
            json_data["path"] = "root@127.0.0.1:/home/root/%s" % filename_w_ext
            json_data["password"] = bmc_pwd
            fw_extra_str = str(fw_extra).lower()
            flash = fw_extra_str if fw_extra_str in [
                "master", "slave", "both"] else "both"
            json_data["flash"] = flash

            if flash == "both":
                print("Installing BMC as master mode...")
                json_data["flash"] = "master"
                r = requests.post(self.bmc_info_url, json=json_data)
                if r.status_code != 200 or 'success' not in r.json().get('result'):
                    print("Failed")
                    return False
                print("Done")
                json_data["flash"] = "slave"

            print("Installing BMC as %s mode..." % json_data["flash"])
            r = requests.post(self.bmc_info_url, json=json_data)
            if r.status_code == 200 and 'success' in r.json().get('result'):
                print("Done, Rebooting BMC.....")
                reboot_dict = dict()
                reboot_dict["reboot"] = "yes"
                r = requests.post(self.bmc_info_url, json=reboot_dict)
                print("Done")
                return True
            else:
                print("Failed")
                return False

        elif fw_type == 'fpga':
            command = 'fpga_prog ' + fw_path
            print("Running command : ", command)
            process = subprocess.Popen(
                command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

            while True:
                output = process.stdout.readline()
                if output == '' and process.poll() is not None:
                    break
                if output:
                    print(output.strip())

            if process.returncode == 0:
                rc = self.__fpga_pci_rescan()
                if rc == 0:
                    return True
                else:
                    print("Fails to load new FPGA firmware")
                    return False
            else:
                return False

        elif 'cpld' in fw_type:
            # Check input
            fw_extra_str = str(fw_extra).upper()
            if ":" in fw_path and ":" in fw_extra_str:
                fw_path_list = fw_path.split(":")
                fw_extra_str_list = fw_extra_str.split(":")
            else:
                fw_path_list = [fw_path]
                fw_extra_str_list = [fw_extra_str]

            if len(fw_path_list) != len(fw_extra_str_list):
                print("Failed: Invalid input")
                return False

            data_list = list(zip(fw_path_list, fw_extra_str_list))
            refresh_img_path = None
            for data in data_list:
                fw_path = data[0]
                fw_extra_str = data[1]

                # Set fw_extra
                fw_extra_str = {
                    "TOP_LC_CPLD": "top_lc",
                    "BOT_LC_CPLD": "bottom_lc",
                    "FAN_CPLD": "fan",
                    "CPU_CPLD": "cpu",
                    "BASE_CPLD": "base",
                    "COMBO_CPLD": "combo",
                    "SW_CPLD": "switch",
                    "REFRESH_CPLD": "refresh"
                }.get(fw_extra_str, None)

                if fw_extra_str == "refresh":
                    refresh_img_path = fw_path
                    continue

                if fw_extra_str is None:
                    print("Failed: Invalid extra information string")
                    return False

                # Uploading image to BMC
                print("Upgrade %s cpld" % fw_extra_str)
                print("Uploading image to BMC...")
                upload_file = self.upload_file_bmc(fw_path)
                if not upload_file:
                    print("Failed: Unable to upload image to BMC")
                    return False

                filename_w_ext = os.path.basename(fw_path)
                json_data = dict()
                json_data["image_path"] = "root@127.0.0.1:/home/root/%s" % filename_w_ext
                json_data["password"] = bmc_pwd
                json_data["device"] = "cpld"
                json_data["reboot"] = "no"
                json_data["type"] = fw_extra_str

                # Call BMC api to install cpld image
                print("Installing...")
                r = requests.post(self.fw_upgrade_url, json=json_data)
                if r.status_code != 200 or 'success' not in r.json().get('result'):
                    print("Failed: Invalid cpld image")
                    return False

                print("%s cpld upgrade completed\n" % fw_extra_str)

            # Refresh CPLD
            if "COMBO_CPLD" in fw_extra_str_list or "BASE_CPLD" in fw_extra_str_list or "CPU_CPLD" in fw_extra_str_list:
                print("Refreshing CPLD...")

                if refresh_img_path is None:
                    print("Failed: Missing refresh image")
                    return False

                fw_path = refresh_img_path
                upload_file = self.upload_file_bmc(fw_path)
                if not upload_file:
                    print("Failed: Unable to upload refresh image to BMC")
                    return False

                filename_w_ext = os.path.basename(fw_path)
                json_data = dict()
                json_data["image_path"] = "root@127.0.0.1:/home/root/%s" % filename_w_ext
                json_data["password"] = bmc_pwd
                json_data["device"] = "cpld"
                json_data["type"] = "enable"
                r = requests.post(self.fw_upgrade_url, json=json_data)
                if r.status_code != 200 or 'success' not in r.json().get('result'):
                    print("Failed: Invalid refresh image")
                    return False

            print("Done")
            return True

        elif 'bios' in fw_type:
            fw_extra_str = str(fw_extra).lower()
            flash = fw_extra_str if fw_extra_str in [
                "master", "slave"] else "master"

            scp_command = 'sudo scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -r %s root@240.1.1.1:/home/root/' % os.path.abspath(
                fw_path)
            child = pexpect.spawn(scp_command)
            i = child.expect(["root@240.1.1.1's password:"], timeout=30)
            if i == 0:
                print("Uploading image to BMC...")
                print("Running command : ", scp_command)
                child.sendline(bmc_pwd)
                data = child.read()
                print(data)
                child.close

            filename_w_ext = os.path.basename(fw_path)
            json_data = dict()
            json_data["image_path"] = "root@127.0.0.1:/home/root/%s" % filename_w_ext
            json_data["password"] = bmc_pwd
            json_data["device"] = "bios"
            json_data["flash"] = flash
            json_data["reboot"] = "no"

            print("Installing BIOS ... ")
            r = requests.post(self.fw_upgrade_url, json=json_data)
            if r.status_code != 200 or 'success' not in r.json().get('result'):
                print("Failed")
                return False
            print("Done")
            return True

        print("Failed: Invalid firmware type")
        return False

    def get_last_upgrade_result(self):
        """
            Get last firmware upgrade information, inlcudes:
            1) FwType: cpld/fpga/bios/bmc(passed by method 'firmware_upgrade'), string
            2) FwPath: path and file name of firmware(passed by method 'firmware_upgrade'), string
            3) FwExtra: designated string, econdings of this string is determined by vendor(passed by method 'firmware_upgrade')
            4) Result: indicates whether the upgrade action is performed and success/failure status if performed. Values should be one of: "DONE"/"FAILED"/"NOT_PERFORMED".
            dict object:
            {
                "FwType": "cpld",
                "FwPath": "/tmp/fw/x86_64-alibaba-as13-48f8h/cpld_come_fan_board.vme"
                "FwExtra": "specific_encoded_string"
                "Result": "DONE"/"FAILED"/"NOT_PERFORMED"
            }
        """
        last_update_dict = dict()

        upgrade_info_req = requests.get(self.fw_upgrade_url)
        if upgrade_info_req.status_code == 200 and 'success' in upgrade_info_req.json().get('result'):
            upgrade_info_json = upgrade_info_req.json()
            for info_json_key in upgrade_info_json.keys():
                if info_json_key == "result" or upgrade_info_json[info_json_key] == "None":
                    continue

                update_dict = dict()
                raw_data = upgrade_info_json[info_json_key][-1]
                raw_data_list = raw_data.split(",")
                fw_path = raw_data_list[1].split("firmware:")[1].strip()
                fw_extra_raw = raw_data_list[0].split(":")[0].strip()
                fw_result_raw = raw_data_list[0].split(":")[1].strip()
                raw_datetime = raw_data_list[2].split("time:")[1].strip()
                reformat_datetime = raw_datetime.replace("CST", "UTC")
                fw_extra_time = datetime.strptime(
                    reformat_datetime, '%a %b %d %H:%M:%S %Z %Y')

                fw_extra_str = fw_extra_raw
                if info_json_key == "CPLD upgrade log":
                    fw_extra_str = {
                        "top_lc": "TOP_LC_CPLD",
                        "bot_lc": "BOT_LC_CPLD",
                        "fan": "FAN_CPLD",
                        "cpu": "CPU_CPLD",
                        "base": "BASE_CPLD",
                        "combo": "COMBO_CPLD",
                        "switch": "SW_CPLD"
                    }.get(fw_extra_raw, None)
                fw_result = "DONE" if fw_result_raw == "success" else fw_result_raw.upper()
                fw_result = "FAILED" if "FAILED" in fw_result else fw_result
                fw_result = "NOT_PERFORMED" if fw_result != "DONE" and fw_result != "FAILED" else fw_result
                update_dict["FwType"] = info_json_key.split(" ")[0].lower()
                update_dict["FwPath"] = fw_path
                update_dict["FwExtra"] = fw_extra_str
                update_dict["Result"] = fw_result
                update_dict["FwTime"] = fw_extra_time
                if last_update_dict == dict() or update_dict["FwTime"] > last_update_dict["FwTime"]:
                    last_update_dict = update_dict

        last_update_dict.pop('FwTime', None)
        return last_update_dict
