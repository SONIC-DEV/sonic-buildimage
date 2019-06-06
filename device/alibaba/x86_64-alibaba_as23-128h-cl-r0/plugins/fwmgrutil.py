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
import json
import logging
from datetime import datetime

try:
    from sonic_fwmgr.fwgmr_base import FwMgrUtilBase
except ImportError as e:
    raise ImportError("%s - required module not found" % str(e))


class FwMgrUtil(FwMgrUtilBase):

    """Platform-specific FwMgrUtil class"""

    def __init__(self):
        self.platform_name = "AS23128h"
        self.onie_config_file = "/host/machine.conf"
        self.bmc_info_url = "http://240.1.1.1:8080/api/sys/bmc"
        self.bmc_raw_command_url = "http://240.1.1.1:8080/api/sys/raw"
        self.fw_upgrade_url = "http://240.1.1.1:8080/api/sys/upgrade"
        self.onie_config_file = "/host/machine.conf"
        self.fw_upgrade_logger_path = "/var/log/fw_upgrade.log"
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

    def __update_fw_upgrade_logger(self, header, message):
        if not os.path.isfile(self.fw_upgrade_logger_path):
            cmd = "sudo touch %s && sudo chmod +x %s" % (
                self.fw_upgrade_logger_path, self.fw_upgrade_logger_path)
            subprocess.Popen(
                cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

        logging.basicConfig(filename=self.fw_upgrade_logger_path,
                            filemode='a',
                            format='%(asctime)s,%(msecs)d %(name)s %(levelname)s %(message)s',
                            datefmt='%H:%M:%S',
                            level=logging.INFO)

        log_message = "%s : %s" % (header, message)
        if header != "last_upgrade_result":
            print(log_message)
        return logging.info(log_message)

    def get_bmc_pass(self):
        if os.path.exists(self.bmc_pwd_path):
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
        return False

    def get_bmc_version(self):
        """Get BMC version from SONiC
        :returns: version string

        """
        bmc_version = None

        bmc_version_key = "OpenBMC Version"
        bmc_info_req = requests.get(self.bmc_info_url, timeout=60)
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
        bmc_pwd = self.get_bmc_pass()
        if i == 0 and bmc_pwd:
            child.sendline(bmc_pwd)
            data = child.read()
            print(data)
            child.close
            return os.path.isfile(fw_path)
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

    def upgrade_logger(self, upgrade_list):
        try:
            with open(self.fw_upgrade_logger_path, 'w') as filetowrite:
                json.dump(upgrade_list, filetowrite)
        except Exception as e:
            pass

    def firmware_upgrade(self, fw_type, fw_path, fw_extra=None):
        """
            @fw_type MANDATORY, firmware type, should be one of the strings: 'cpld', 'fpga', 'bios', 'bmc'
            @fw_path MANDATORY, target firmware file
            @fw_extra OPTIONAL, extra information string,

            for fw_type 'cpld' and 'fpga': it can be used to indicate specific cpld, such as 'cpld1', 'cpld2', ...
                or 'cpld_fan_come_board', etc. If None, upgrade all CPLD/FPGA firmware. for fw_type 'bios' and 'bmc',
                 value should be one of 'master' or 'slave' or 'both'
        """
        fw_type = fw_type.lower()
        upgrade_list = []
        bmc_pwd = self.get_bmc_pass()
        if not bmc_pwd and fw_type != "fpga":
            print("Failed: BMC credential not found")
            return False

        if fw_type == 'bmc':
            # Copy BMC image file to BMC
            print("BMC Upgrade")
            print("Uploading image to BMC...")
            upload_file = self.upload_file_bmc(fw_path)

            if not upload_file:
                print("Failed: Unable to upload BMC image to BMC")
                return False

            filename_w_ext = os.path.basename(fw_path)
            json_data = dict()
            json_data["path"] = "root@127.0.0.1:/home/root/%s" % filename_w_ext
            json_data["password"] = bmc_pwd

            # Set flash type
            fw_extra_str = str(fw_extra).lower()
            current_bmc = self.get_running_bmc()
            flash = fw_extra_str if fw_extra_str in [
                "master", "slave", "both"] else "both"
            if fw_extra_str == "pingpong":
                flash = "master" if current_bmc == "slave" else "slave"
            json_data["flash"] = flash

            # Install BMC
            if flash == "both":
                print("Installing BMC as master mode...")
                json_data["flash"] = "master"
                r = requests.post(self.bmc_info_url, json=json_data)
                if r.status_code != 200 or 'success' not in r.json().get('result'):
                    print("Failed: BMC API report error code %d" %
                          r.status_code)
                    return False
                print("Done")
                json_data["flash"] = "slave"

            print("Installing BMC as %s mode..." % json_data["flash"])
            r = requests.post(self.bmc_info_url, json=json_data)
            if r.status_code == 200 and 'success' in r.json().get('result'):
                if fw_extra_str == "pingpong":
                    print("Switch to boot from %s" % flash)
                    self.set_bmc_boot_flash(flash)
                    print("Rebooting BMC.....")
                    if not self.reboot_bmc():
                        return False
                else:
                    print("Rebooting BMC in old fashion.....")
                    reboot_dict = {}
                    reboot_dict["reboot"] = "yes"
                    r = requests.post(self.bmc_info_url, json=reboot_dict)
                print("Done")
            else:
                print("Failed: Unable to install BMC image")
                return False

            upgrade_list.append(filename_w_ext.lower())
            return True

        elif fw_type == 'fpga':
            print("FPGA Upgrade")
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
                if rc != 0:
                    print("Failed: Unable to load new FPGA firmware")
                    return False
            else:
                print("Failed: Invalid fpga image")
                return False

            print("Done")
            return True

        elif 'cpld' in fw_type:
            print("CPLD Upgrade")
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
                    "SW_CPLD1": "switch",
                    "SW_CPLD2": "switch",
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
                upgrade_list.append(filename_w_ext.lower())

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
                upgrade_list.append(filename_w_ext.lower())
                self.upgrade_logger(upgrade_list)
                json_data = dict()
                json_data["image_path"] = "root@127.0.0.1:/home/root/%s" % filename_w_ext
                json_data["password"] = bmc_pwd
                json_data["device"] = "cpld"
                json_data["type"] = "enable"
                r = requests.post(self.fw_upgrade_url, json=json_data)
                if r.status_code != 200 or 'success' not in r.json().get('result'):
                    print("Failed: Invalid refresh image")
                    return False

            self.upgrade_logger(upgrade_list)
            print("Done")
            return True

        elif 'bios' in fw_type:
            print("BIOS Upgrade")
            fw_extra_str = str(fw_extra).lower()
            flash = fw_extra_str if fw_extra_str in [
                "master", "slave"] else "master"

            scp_command = 'sudo scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -r %s root@240.1.1.1:/home/root/' % os.path.abspath(
                fw_path)
            child = pexpect.spawn(scp_command)
            i = child.expect(["root@240.1.1.1's password:"], timeout=30)
            if i != 0:
                print("Failed: Unable to connect to BMC")
                return False

            print("Uploading image to BMC...")
            child.sendline(bmc_pwd)
            data = child.read()
            print(data)
            child.close
            if not os.path.exists(fw_path):
                return False

            json_data = dict()
            json_data["data"] = "/usr/bin/ipmitool -b 1 -t 0x2c raw 0x2e 0xdf 0x57 0x01 0x00 0x01"
            r = requests.post(self.bmc_raw_command_url, json=json_data)
            if r.status_code != 200:
                print("Failed")
                return False

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

            upgrade_list.append(filename_w_ext.lower())
            print("Done")
        else:
            print("Failed: Invalid firmware type")
            return False

        return True

    def get_last_upgrade_result(self):
        """
            Get last firmware upgrade information, inlcudes:
            1) FwType: cpld/fpga/bios/bmc(passed by method 'firmware_upgrade'), string
            2) FwPath: path and file name of firmware(passed by method 'firmware_upgrade'), string
            3) FwExtra: designated string, econdings of this string is determined by vendor(passed by method 'firmware_upgrade')
            4) Result: indicates whether the upgrade action is performed and success/failure status if performed. Values should be one of: "DONE"/"FAILED"/"NOT_PERFORMED".
            list of object:
            [
                {
                    "FwType": "cpld",
                    "FwPath": "cpu_cpld.vme"
                    "FwExtra":"CPU_CPLD"
                    "Result": "DONE"
                },
                {
                    "FwType": "cpld",
                    "FwPath": "fan_cpld.vme"
                    "FwExtra": "FAN_CPLD"
                    "Result": "FAILED"
                },
                {
                    "FwType": "cpld",
                    "FwPath": "refresh_cpld.vme"
                    "FwExtra": "REFRESH_CPLD"
                    "Result": "DONE"
                }
            ]
        """
        last_update_list = []
        local_log = []

        if os.path.exists(self.fw_upgrade_logger_path):
            with open(self.fw_upgrade_logger_path, 'r') as file:
                data = file.read()

        local_log = json.loads(data)
        upgrade_info_req = requests.get(self.fw_upgrade_url)
        if upgrade_info_req.status_code == 200 and 'success' in upgrade_info_req.json().get('result'):
            upgrade_info_json = upgrade_info_req.json()

            for info_json_key in upgrade_info_json.keys():
                if info_json_key == "result" or upgrade_info_json[info_json_key] == "None":
                    continue

                for x in range(0, len(upgrade_info_json[info_json_key])):
                    update_dict = dict()
                    index = x if info_json_key == "CPLD upgrade log" else -1
                    raw_data = upgrade_info_json[info_json_key][index]
                    raw_data_list = raw_data.split(",")
                    fw_path = raw_data_list[1].split("firmware:")[1].strip()
                    fw_extra_raw = raw_data_list[0].split(":")[0].strip()
                    fw_result_raw = raw_data_list[0].split(":")[1].strip()
                    raw_datetime = raw_data_list[2].split("time:")[1].strip()
                    reformat_datetime = raw_datetime.replace("CST", "UTC")
                    fw_extra_time = datetime.strptime(
                        reformat_datetime, '%a %b %d %H:%M:%S %Z %Y')
                    fw_extra_str = {
                        "top_lc": "TOP_LC_CPLD",
                        "bot_lc": "BOT_LC_CPLD",
                        "fan": "FAN_CPLD",
                        "cpu": "CPU_CPLD",
                        "base": "BASE_CPLD",
                        "combo": "COMBO_CPLD",
                        "switch": "SW_CPLD",
                        "enable": "REFRESH_CPLD",
                    }.get(fw_extra_raw, fw_extra_raw)
                    fw_result = "DONE" if fw_result_raw == "success" else fw_result_raw.upper()
                    fw_result = "FAILED" if "FAILED" in fw_result else fw_result
                    fw_result = "NOT_PERFORMED" if fw_result != "DONE" and fw_result != "FAILED" else fw_result
                    update_dict["FwType"] = info_json_key.split(" ")[0].lower()
                    update_dict["FwPath"] = fw_path
                    update_dict["FwExtra"] = fw_extra_str
                    update_dict["Result"] = fw_result
                    update_dict["FwTime"] = fw_extra_time
                    last_update_list.append(update_dict)
                    if info_json_key != "CPLD upgrade log":
                        break

        last_update_list = sorted(last_update_list, key=lambda i: i['FwTime'])
        map(lambda d: d.pop('FwTime', None), last_update_list)
        if len(last_update_list) >= len(local_log) and len(last_update_list) != 0:
            if str(last_update_list[-1].get('FwType')).lower() != 'cpld' or len(local_log) <= 1:
                last_update_list = [last_update_list[-1]]
            else:
                remote_list = last_update_list[-len(local_log):]
                remote_list_value = [d['FwPath'].lower()
                                     for d in remote_list if 'FwPath' in d]
                last_update_list = remote_list if remote_list_value == local_log else [
                    last_update_list[-1]]
        elif len(last_update_list) != 0:
            last_update_list = [last_update_list[-1]]

        return last_update_list

    def firmware_program(self, fw_type, fw_path, fw_extra=None):
        """
            Program FPGA and/or CPLD firmware only, but do not refresh them

            @param fw_type value can be: FPGA, CPLD
            @param fw_path a string of firmware file path, seperated by ':', it should
                        match the sequence of param @fw_type
            @param fw_extra a string of firmware subtype, i.e CPU_CPLD, BOARD_CPLD,
                            FAN_CPLD, LC_CPLD, etc. Subtypes are seperated by ':'
            @return True when all required firmware is program succefully,
                    False otherwise.

            Example:
                self.firmware_program("CPLD", "/cpu_cpld.vme:/lc_cpld", \
                                    "CPU_CPLD:LC_CPLD")
                or
                self.firmware_program("FPGA", "/fpga.bin")
        """
        fw_type = fw_type.lower()
        bmc_pwd = self.get_bmc_pass()
        if not bmc_pwd and fw_type != "fpga":
            self.__update_fw_upgrade_logger(
                "fw_upgrade", "fail, message=BMC credential not found")
            return False

        if fw_type == 'fpga':
            last_fw_upgrade = ["FPGA", fw_path, None, "FAILED"]
            self.__update_fw_upgrade_logger(
                "fpga_upgrade", "start FPGA upgrade")

            if not os.path.isfile(fw_path):
                self.__update_fw_upgrade_logger(
                    "fpga_upgrade", "fail, message=FPGA image not found %s" % fw_path)
                self.__update_fw_upgrade_logger(
                    "last_upgrade_result", str(last_fw_upgrade))
                return False

            command = 'fpga_prog ' + fw_path
            print("Running command: %s" % command)
            process = subprocess.Popen(
                command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

            while True:
                output = process.stdout.readline()
                if output == '' and process.poll() is not None:
                    break

            rc = process.returncode
            if rc != 0:
                self.__update_fw_upgrade_logger(
                    "fw_upgrade", "fail, message=Unable to install FPGA")
                self.__update_fw_upgrade_logger(
                    "last_upgrade_result", str(last_fw_upgrade))
                return False

            self.__update_fw_upgrade_logger("fpga_upgrade", "done")
            last_fw_upgrade[3] = "DONE"
            self.__update_fw_upgrade_logger(
                "last_upgrade_result", str(last_fw_upgrade))
            return True

        elif 'cpld' in fw_type:
            self.__update_fw_upgrade_logger(
                "cpld_upgrade", "start CPLD upgrade")

            # Check input
            fw_extra_str = str(fw_extra).upper()
            if ":" in fw_path and ":" in fw_extra_str:
                fw_path_list = fw_path.split(":")
                fw_extra_str_list = fw_extra_str.split(":")
            else:
                fw_path_list = [fw_path]
                fw_extra_str_list = [fw_extra_str]

            if len(fw_path_list) != len(fw_extra_str_list):
                self.__update_fw_upgrade_logger(
                    "cpld_upgrade", "fail, message=Invalid input")
                return False

            cpld_result_list = []
            data_list = list(zip(fw_path_list, fw_extra_str_list))
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
                    "SW_CPLD1": "switch",
                    "SW_CPLD2": "switch"
                }.get(fw_extra_str, None)

                self.__update_fw_upgrade_logger(
                    "cpld_upgrade", "start %s upgrade" % data[1])
                upgrade_result = "FAILED"
                for x in range(1, 4):
                    # Set fw_extra
                    if x > 1:
                        self.__update_fw_upgrade_logger(
                            "cpld_upgrade", "fail, message=Retry to upgrade %s" % data[1])

                    elif fw_extra_str is None:
                        self.__update_fw_upgrade_logger(
                            "cpld_upgrade", "fail, message=Invalid extra information string %s" % data[1])
                        break
                    elif not os.path.isfile(os.path.abspath(fw_path)):
                        self.__update_fw_upgrade_logger(
                            "cpld_upgrade", "fail, message=CPLD image not found %s" % fw_path)
                        break

                    # Install cpld image via ispvm tool
                    print("Installing...")
                    command = 'ispvm %s' % fw_path
                    if fw_extra_str in ["top_lc", "bottom_lc"]:
                        option = 1 if fw_extra_str == "top_lc" else 2
                        command = "ispvm -c %d %s" % (option,
                                                      os.path.abspath(fw_path))
                    print("Running command : %s" % command)
                    process = subprocess.Popen(
                        command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

                    while True:
                        output = process.stdout.readline()
                        if output == '' and process.poll() is not None:
                            break

                    rc = process.returncode
                    if rc != 0:
                        self.__update_fw_upgrade_logger(
                            "cpld_upgrade", "fail, message=Unable to install CPLD")
                        continue

                    upgrade_result = "DONE"
                    self.__update_fw_upgrade_logger("cpld_upgrade", "done")
                    break
                cpld_result_list.append(upgrade_result)

            last_fw_upgrade = ["CPLD", ":".join(
                fw_path_list), ":".join(fw_extra_str_list), ":".join(cpld_result_list)]
            self.__update_fw_upgrade_logger(
                "last_upgrade_result", str(last_fw_upgrade))
            return "FAILED" not in cpld_result_list
        else:
            self.__update_fw_upgrade_logger(
                "fw_upgrade", "fail, message=Invalid firmware type")
            return False

        return True

    def firmware_refresh(self, fpga_list, cpld_list, fw_extra=None):
        """
            Refresh firmware and take extra action when necessary.
            @param fpga_list a list of FPGA names
            @param cpld_list a list of CPLD names
            @return True if refresh succefully and no power cycle action is taken.

            @Note extra action should be: power cycle the whole system(except BMC) when
                                        CPU_CPLD or BOARD_CPLD or FPGA is refreshed.
                                        No operation if the power cycle is not needed.

            Example:
            self.firmware_refresh(
                ["FPGA"], ["BASE_CPLD", "LC_CPLD"],"/tmp/fw/refresh.vme")
            or
            self.firmware_refresh(["FPGA"], None, None)
            or
            self.firmware_refresh(None, ["FAN_CPLD", "LC1_CPLD", "BASE_CPLD"],
                                  "/tmp/fw/fan_refresh.vme:none:/tmp/fw/base_refresh.vme")
        """
        upgrade_list = []
        if not fpga_list and not cpld_list:
            return False

        if cpld_list and ("BASE_CPLD" in cpld_list or "FAN_CPLD" in cpld_list):
            self.__update_fw_upgrade_logger(
                "cpld_refresh", "start cpld refresh")
            fw_path_list = fw_extra.split(':')
            command = "echo "
            if len(fw_path_list) != len(cpld_list):
                self.__update_fw_upgrade_logger(
                    "cpld_refresh", "fail, message=Invalid fw_extra")
                return False

            for idx in range(0, len(cpld_list)):
                fw_path = fw_path_list[idx]
                refresh_type = {
                    "BASE_CPLD": "base",
                    "FAN_CPLD": "fan"
                }.get(cpld_list[idx], None)

                if not refresh_type:
                    continue
                elif not self.upload_file_bmc(fw_path):
                    self.__update_fw_upgrade_logger(
                        "cpld_refresh", "fail, message=Unable to upload refresh image to BMC")
                    return False
                else:
                    filename_w_ext = os.path.basename(fw_path)
                    upgrade_list.append(filename_w_ext.lower())

                    sub_command = "%s /home/root/%s > /tmp/cpld_refresh " % (
                        refresh_type, filename_w_ext)
                    command += sub_command

            json_data = dict()
            json_data["data"] = command
            r = requests.post(self.bmc_raw_command_url, json=json_data)
            if r.status_code != 200:
                self.__update_fw_upgrade_logger(
                    "cpld_refresh", "fail, message=%d Invalid refresh image" % r.status_code)
                return False
            self.__update_fw_upgrade_logger("cpld_refresh", "done")

        elif cpld_list:
            self.__update_fw_upgrade_logger(
                "cpld_refresh", "start CPLD refresh")
            json_data = dict()
            json_data["data"] = "echo cpu_cpld > /tmp/cpld_refresh"
            r = requests.post(self.bmc_raw_command_url, json=json_data)
            if r.status_code != 200:
                self.__update_fw_upgrade_logger(
                    "cpld_refresh", "fail, message=%d Unable to load new FPGA" % r.status_code)
                return False
            self.__update_fw_upgrade_logger("cpld_refresh", "done")

        elif fpga_list:
            self.__update_fw_upgrade_logger(
                "fpga_refresh", "start FPGA refresh")
            json_data = dict()
            json_data["data"] = "echo fpga > /tmp/cpld_refresh"
            r = requests.post(self.bmc_raw_command_url, json=json_data)
            if r.status_code != 200:
                self.__update_fw_upgrade_logger(
                    "cpld_refresh", "fail, message=%d Unable to load new FPGA" % r.status_code)
                return False
            self.__update_fw_upgrade_logger("fpga_refresh", "done")

        else:
            self.__update_fw_upgrade_logger(
                "fw_refresh", "fail, message=Invalid input")
            return False

        return True

    def get_running_bmc(self):
        """
            Get booting flash of running BMC.
            @return a string, "master" or "slave"
        """
        json_data = dict()
        json_data["data"] = "/usr/local/bin/boot_info.sh"
        r = requests.post(self.bmc_raw_command_url, json=json_data)
        try:
            boot_info_list = r.json().get('result')
            for boot_info_raw in boot_info_list:
                boot_info = boot_info_raw.split(":")
                if "Current Boot Code Source" in boot_info[0]:
                    flash = "master" if "master "in boot_info[1].lower(
                    ) else "slave"
                    return flash
            raise Exception(
                "Error: Unable to detect booting flash of running BMC")
        except Exception as e:
            raise Exception(e)

    def set_bmc_boot_flash(self, flash):
        """
            Set booting flash of BMC
            @param flash should be "master" or "slave"
        """
        if flash.lower() not in ["master", "slave"]:
            return False
        json_data = dict()
        json_data["data"] = "source /usr/local/bin/openbmc-utils.sh;bmc_reboot %s" % flash
        r = requests.post(self.bmc_raw_command_url, json=json_data)
        if r.status_code != 200:
            return False
        return True

    def reboot_bmc(self):
        """
            Reboot BMC
        """
        json_data = dict()
        json_data["data"] = "source /usr/local/bin/openbmc-utils.sh;bmc_reboot reboot"
        r = requests.post(self.bmc_raw_command_url, json=json_data)
        if r.status_code != 200:
            return False
        return True

    def get_current_bios(self):
        """
            # Get booting bios image of current running host OS
            # @return a string, "master" or "slave"
        """
        json_data = dict()
        json_data["data"] = "source /usr/local/bin/openbmc-utils.sh;come_boot_info"
        r = requests.post(self.bmc_raw_command_url, json=json_data)
        try:
            cpu_boot_info_list = r.json().get('result')
            for cpu_boot_info_raw in cpu_boot_info_list:
                if "COMe CPU boots from BIOS" in cpu_boot_info_raw:
                    bios_image = "master" if "master "in cpu_boot_info_raw.lower(
                    ) else "slave"
                    return bios_image
            raise Exception(
                "Error: Unable to detect current running bios image")
        except Exception as e:
            raise Exception(e)
