#!/usr/bin/python


import subprocess
import requests
import os
import pexpect
import base64
import time
import json
import logging
import ast
from datetime import datetime

try:
    from sonic_fwmgr.fwgmr_base import FwMgrUtilBase
except ImportError as e:
    raise ImportError("%s - required module not found" % str(e))


class FwMgrUtil(FwMgrUtilBase):
    BMC_REQ_BASE_URI = "http://240.1.1.1:8080/api"
    ONIE_CFG_FILE = "/host/machine.conf"

    def __init__(self):
        self.platform_name = "AS1348f8h"
        self.bmc_info_uri = "/".join([self.BMC_REQ_BASE_URI, "bmc/info"])
        self.bmc_nextboot_uri = "/".join([self.BMC_REQ_BASE_URI, "bmc/nextboot"])
        self.bmc_reboot_uri = "/".join([self.BMC_REQ_BASE_URI, "bmc/reboot"])
        self.bios_nextboot_uri = "/".join([self.BMC_REQ_BASE_URI, "firmware/biosnextboot"])
        self.fw_upgrade_uri = "/".join([self.BMC_REQ_BASE_URI, "firmware/upgrade"])
        self.fw_refresh_uri = "/".join([self.BMC_REQ_BASE_URI, "firmware/refresh"])
        self.bios_boot_uri = "/".join([self.BMC_REQ_BASE_URI, "misc/biosbootstatus"])

        # BMC 1.3.8
        self.old_raw_cmd_uri = "http://240.1.1.1:8080/api/sys/raw"
        self.old_bmc_info_uri = "http://240.1.1.1:8080/api/sys/bmc"

        self.fw_upgrade_logger_path = "/var/log/fw_upgrade.log"
        self.cpld_ver_uri = "/".join([self.BMC_REQ_BASE_URI, "misc/cpldversion"])

        self.cpld_ver_info = {
            "CPLD_B": {
                "path": "/sys/devices/platform/%s.cpldb/getreg" % self.platform_name,
                "offset": "0xA100"
            },
            "CPLD_C": {
                "path": "/sys/devices/platform/%s.cpldb/getreg" % self.platform_name,
                "offset": "0xA1E0"
            },
            "CPLD_1": {
                "path": "/sys/devices/platform/%s.switchboard/CPLD1/getreg" % self.platform_name,
                "offset": "0x00"
            },
            "CPLD_2": {
                "path": "/sys/devices/platform/%s.switchboard/CPLD2/getreg" % self.platform_name,
                "offset": "0x00"
            },
            "CPLD_3": {
                "path": "/sys/devices/platform/%s.switchboard/CPLD3/getreg" % self.platform_name,
                "offset": "0x00"
            },
            "CPLD_4": {
                "path": "/sys/devices/platform/%s.switchboard/CPLD4/getreg" % self.platform_name,
                "offset": "0x00"
            },
            "CPLD_FAN": {
                "path": "bmc",
                "offset": "0x00"
            }
        }
        self.fpga_version_path = "/sys/devices/platform/%s.switchboard/FPGA/getreg" % self.platform_name
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

    def old_bmc_set_next_boot(self,flash):
        #set_bmc_boot_flash
        json_tmp = dict()
        json_tmp["data"] = "source /usr/local/bin/openbmc-utils.sh;bmc_reboot %s" % flash
        requests.post(self.old_raw_cmd_uri, json=json_tmp)

        # reboot
        json_tmp = dict()
        json_tmp["data"] = "source /usr/local/bin/openbmc-utils.sh;bmc_reboot reboot"
        requests.post(self.old_raw_cmd_uri, json=json_tmp)
        return

    def old_bmc_get_version(self):

        bmc_version = None
        bmc_version_key = "OpenBMC Version"
        bmc_info_req = requests.get(self.old_bmc_info_uri, timeout=60)
        if bmc_info_req.status_code == 200:
            bmc_info_json = bmc_info_req.json()
            bmc_info = bmc_info_json.get('Information')
            bmc_version = bmc_info.get(bmc_version_key)
        return str(bmc_version)

    def old_bmc_upgrade(self, fw_path, fw_extra):

        fw_extra_str = str(fw_extra).lower()

        json_data = dict()
        json_data["path"] = "admin@240.1.1.2:%s" % fw_path
        json_data["password"] = "admin"

        # get running bmc
        json_tmp = dict()
        json_tmp["data"] = "/usr/local/bin/boot_info.sh"
        r = requests.post(self.old_raw_cmd_uri, json=json_tmp)
        current_bmc = None
        if r.status_code == 200:
            boot_info_list = r.json().get('result')
            for boot_info_raw in boot_info_list:
                boot_info = boot_info_raw.split(":")
                if "Current Boot Code Source" in boot_info[0]:
                    flash = "master" if "master "in boot_info[1].lower() else "slave"
                    current_bmc = flash

        if not current_bmc:
            print("Fail, message = Unable to detech current bmc")
            return False

        # umount /mnt/data
        umount_json = dict()
        umount_json["data"] = "pkill rsyslogd; umount -f /mnt/data/"
        r = requests.post(self.old_raw_cmd_uri, json=umount_json)
        if r.status_code != 200:
            print("Fail, message = Unable to umount /mnt/data")
            return False

        # Set flash
        flash = fw_extra_str if fw_extra_str in [
            "master", "slave", "both"] else "both"
        if fw_extra_str == "pingpong":
            flash = "master" if current_bmc == "slave" else "slave"
        json_data["flash"] = flash

        # Install BMC
        if flash == "both":
            print("Install BMC as master mode")
            json_data["flash"] = "master"
            r = requests.post(self.old_bmc_info_uri, json=json_data)
            if r.status_code != 200 or 'success' not in r.json().get('result'):
                cause = str(r.status_code) if r.status_code != 200 else r.json().get('result')
                print("Fail, message = BMC API report error code %d" % r.cause)
                return False
            json_data["flash"] = "slave"

        print("Install BMC as %s mode" % json_data["flash"])
        r = requests.post(self.old_bmc_info_uri, json=json_data)
        if r.status_code == 200 and 'success' in r.json().get('result'):

            if fw_extra_str == "pingpong":
                flash = "master" if current_bmc == "slave" else "slave"
                print("Switch to boot from %s" % flash)

                #set_bmc_boot_flash
                self.old_bmc_set_next_boot(flash)
            else:
                # Change boot flash if required
                if current_bmc != flash and flash != "both":
                    # Set desired boot flash
                    self.old_bmc_set_next_boot(flash)
                else:
                    reboot_dict = {}
                    reboot_dict["reboot"] = "yes"
                    r = requests.post(self.old_bmc_info_uri, json=reboot_dict)
        elif r.status_code == 200:
            print("Fail, message = %s" % r.json().get('result'))
            return False
        else:
            print("Fail, message = Unable to install BMC image")
            return False

        print("Done")

        return True


    def get_bmc_pass(self):
        if os.path.exists(self.bmc_pwd_path):
            with open(self.bmc_pwd_path) as fh:
                data = fh.read()

            key = "bmc"
            dec = []
            enc = base64.urlsafe_b64decode(data)
            for i in range(len(enc)):
                key_c = key[i % len(key)]
                dec_c = chr((256 + ord(enc[i]) - ord(key_c)) % 256)
                dec.append(dec_c)
            return "".join(dec)
        return False

    def get_from_bmc(self, uri):
        resp = requests.get(uri)
        if not resp:
            return None

        data = resp.json()
        if not data or "data" not in data or "status" not in data:
            return None

        if data["status"] != "OK":
            return None

        return data["data"]

    def get_bmc_version(self):
        bmc_ver = "N/A"
        data = self.get_from_bmc(self.bmc_info_uri)
        if not data or "Version" not in data:
            return bmc_ver

        return data["Version"]

    def get_bmc_flash(self):
        flash = "N/A"
        data = self.get_from_bmc(self.bmc_info_uri)
        if not data or "Flash" not in data:
            return flash

        return data["Flash"]

    def post_to_bmc(self, uri, data, resp_required=True):
        try:
            resp = requests.post(uri, json=data)
        except Exception as e:
            if not resp_required:
                return True
            return False

        if not resp_required:
            return True
        elif not resp:
            print "No response"
            return False

        data = resp.json()
        if "status" not in data:
            print "status not in data"
            return False

        if data["status"] != "OK":
            print "status <%s> is not in OK" % data["status"]
            return False

        return True

    def upload_to_bmc(self, fw_path):
        scp_command = 'sudo scp -o StrictHostKeyChecking=no -o ' \
                      'UserKnownHostsFile=/dev/null -r %s root@240.1.1.1:/tmp/' \
                      % os.path.abspath(fw_path)
        for n in range(0,3):
            child = pexpect.spawn(scp_command, timeout=120)
            expect_list = [pexpect.EOF, pexpect.TIMEOUT, "'s password:"]
            i = child.expect(expect_list, timeout=120)
            bmc_pwd = self.get_bmc_pass()
            if i == 2 and bmc_pwd != None:
                child.sendline(bmc_pwd)
                data = child.read()
                child.close()
                return os.path.isfile(fw_path)
            elif i == 0:
                return True
            else:
                print "Failed to scp %s to BMC, index %d, retry %d" % (fw_path, i, n)
                continue
        print "Failed to scp %s to BMC, index %d" % (fw_path, i)
        return False

    def get_cpld_version(self):
        cpld_version_dict = {}
        for cpld_name, info in self.cpld_ver_info.items():
            if info["path"] == "bmc":
                cpld_ver = self.get_from_bmc(self.cpld_ver_uri)
                if cpld_ver and cpld_name in cpld_ver:
                    cpld_ver_str = cpld_ver[cpld_name]
                else:
                    cpld_ver_str = "None"
            else:
                cpld_ver = self.__get_register_value(info["path"], info["offset"])

                cpld_ver_str = "None" if cpld_ver is "None" else \
                               "{}.{}".format(int(cpld_ver[2], 16), int(cpld_ver[3], 16))
            cpld_version_dict[cpld_name] = cpld_ver_str

        return cpld_version_dict

    def get_bios_version(self):
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
        onie_verison = None

        onie_version_keys = "onie_version"
        onie_config_file = open(self.ONIE_CFG_FILE, "r")
        for line in onie_config_file.readlines():
            if onie_version_keys in line:
                onie_version_raw = line.split('=')
                onie_verison = onie_version_raw[1].strip()
                break
        onie_config_file.close()
        return str(onie_verison)

    def get_pcie_version(self):
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
        bmc_pwd = self.get_bmc_pass()
        if not bmc_pwd and fw_type != "fpga":
            print("Failed: BMC credential not found")
            return False

        if fw_type == 'bmc':
            # Copy BMC image file to BMC
            print("BMC Upgrade")
            print("Uploading image to BMC...")
            if not self.upload_to_bmc(fw_path):
                print("Failed: Unable to upload BMC image to BMC")
                return False
            print("Upload bmc image %s to BMC done" % fw_path)
            cur_bmc_ver = self.get_bmc_version()
            chk_old_bmc = self.old_bmc_get_version()
            if cur_bmc_ver == 'N/A' and chk_old_bmc:
                return self.old_bmc_upgrade(fw_path, fw_extra)

            # Fill json param, "Name", "Path", "Flash"
            image_name = os.path.basename(fw_path)
            json_data = {}
            json_data["Name"] = "bmc"
            json_data["Path"] = "/tmp/%s" % image_name

            # Determine which flash to upgrade
            fw_extra_str = str(fw_extra).lower()
            current_bmc = self.get_bmc_flash()
            flash_list = ["master", "slave", "both"]
            if fw_extra_str not in flash_list:
                if fw_extra_str != "pingpong":
                    print "BMC flash should be master/slave/both/pingpong"
                    return False

            if fw_extra_str == "pingpong":
                flash = "slave" if current_bmc == "master" else "master"
            else:
                flash = fw_extra_str
            json_data["Flash"] = flash

            # Send the upgrade request BMC
            if not self.post_to_bmc(self.fw_upgrade_uri, json_data):
                print "Failed to upgrade BMC %s flash" % flash
                return False

            # Change boot flash if required
            if current_bmc != flash and flash != "both":
                # Set desired boot flash
                print("Current BMC boot flash %s, user requested %s" % (current_bmc, flash))
                json_data = {}
                json_data["Flash"] = flash
                if not self.post_to_bmc(self.bmc_nextboot_uri, json_data):
                    print "Failed to set BMC next boot to %s" % flash
                    return False

            # Reboot BMC
            print("Upgrade BMC %s done, reboot it" % fw_extra_str)
            if not self.reboot_bmc():
                print "Failed to reboot BMC after upgrade"
                return False

            print("Upgrade BMC %s full process done" % fw_extra_str)
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
        elif 'bios' in fw_type:
            print("BIOS Upgrade")

            fw_extra_str = str(fw_extra).lower()
            flash = fw_extra_str if fw_extra_str in ["master", "slave"] else "master"

            print("Uploading BIOS image %s to BMC..." % fw_path)
            if not self.upload_to_bmc(fw_path):
                print("Failed to upload %s to bmc" % fw_path)
                return False

            image_name = os.path.basename(fw_path)
            json_data = {}
            json_data["Name"] = "bios"
            json_data["Path"] = "/tmp/%s" % image_name
            json_data["Flash"] = flash

            if not self.post_to_bmc(self.fw_upgrade_uri, json_data):
                print "Failed to upgrade %s BIOS" % flash
                return False

            return True

    def get_last_upgrade_result(self):
        """
            Get last firmware upgrade information, inlcudes:
            1) FwType: cpld/fpga/bios/bmc(passed by method 'firmware_upgrade'), string
            2) FwPath: path and file name of firmware(passed by method 'firmware_upgrade'), string
            3) FwExtra: designated string, econdings of this string is determined by vendor(passed by method 'firmware_program')
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
                }
            ]
        """
        last_update_list = []

        if os.path.exists(self.fw_upgrade_logger_path):
            with open(self.fw_upgrade_logger_path, 'r') as fh:
                lines = fh.read().splitlines()

            upgrade_txt = [i for i in reversed(
                lines) if "last_upgrade_result" in i]
            if len(upgrade_txt) > 0:
                last_upgrade_txt = upgrade_txt[0].split(
                    "last_upgrade_result : ")
                last_upgrade_list = ast.literal_eval(last_upgrade_txt[1])
                for x in range(0, len(last_upgrade_list[1].split(":"))):
                    upgrade_dict = {}
                    upgrade_dict["FwType"] = last_upgrade_list[0].lower()
                    upgrade_dict["FwPath"] = last_upgrade_list[1].split(":")[x]
                    upgrade_dict["FwExtra"] = last_upgrade_list[2].split(":")[
                        x] if last_upgrade_list[2] else "None"
                    upgrade_dict["Result"] = last_upgrade_list[3].split(":")[x]
                    last_update_list.append(upgrade_dict)

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

                # +++ add by maxwill for cpld upgrade index +++ #
                cpld_chnl_index = {
                      "BASE_CPLD": 0,
                      "CPU_CPLD": 1,
                      "SW_CPLD": 3,
                      "FAN_CPLD": 2
                }

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
                    
                    # +++ add by maxwill for cpld upgrade index +++ #
                    index = int(cpld_chnl_index[str(fw_extra).upper()])
                    command = 'ispvm -i %d %s' % (index, fw_path)
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
        fw_names = []
        fw_files = []
        # FPGA list may contain FPGA and BIOS
        if fpga_list:
            for name in fpga_list:
                fw_names.append(name)
                fw_files.append("/tmp/none")

        if cpld_list:
            for name in cpld_list:
                fw_names.append(name)

        if fw_extra:
            img_list = fw_extra.split(":")
            for fpath in img_list:
                if fpath == "none":
                    continue

                fname = os.path.basename(fpath)
                bmc_fpath = "/tmp/%s" % fname
                fw_files.append(bmc_fpath)

                if os.path.exists(fpath) and os.path.isfile(fpath):
                    # upload refresh file to bmc
                    if not self.upload_to_bmc(fpath):
                        return False

        data = {}
        data["Names"] = fw_names
        data["Paths"] = fw_files
        #j = json.dumps(data, sort_keys=True, indent=4, separators=(',', ': '))
        #print j
        if not self.post_to_bmc(self.fw_refresh_uri, data):
            print("Failed to refresh firmware")
            return False

        return True

    def set_bmc_boot_flash(self, flash):
        """
            Set booting flash of BMC
            @param flash should be "master" or "slave"
        """
        flash = str(flash).lower()

        if flash not in ["master", "slave"]:
            return False

        json_data = {}
        json_data["Flash"] = flash
        if not self.post_to_bmc(self.bmc_nextboot_uri, json_data):
            return False
        return True

    def reboot_bmc(self):
        """
            Reboot BMC
        """
        if not self.post_to_bmc(self.bmc_reboot_uri, {}, resp_required=False):
            return False
        return True

    def get_current_bios(self):
        """
            # Get booting bios image of current running host OS
            # @return a string, "master" or "slave"
        """
        bios_ver = "N/A"
        data = self.get_from_bmc(self.bmc_info_uri)
        if not data or "Flash" not in data:
            return bios_ver

        return data["Flash"]

    def get_running_bmc(self):
        """
            Get booting flash of running BMC.
            @return a string, "master" or "slave"
        """
        flash = "N/A"
        data = self.get_from_bmc(self.bmc_info_uri)
        if not data or "Flash" not in data:
            return flash

        return data["Flash"]

    def get_bios_next_boot(self):
        """
            # Get booting bios image of next booting host OS
            # @return a string, "master" or "slave"
        """
        flash = "N/A"
        data = self.get_from_bmc(self.bios_nextboot_uri)
        if not data or "Flash" not in data:
            return flash

        return data["Flash"]

    def set_bios_next_boot(self, flash):
        """
            # Set booting bios image of next booting host OS
            # @return a string, "master" or "slave"
        """
        flash = str(flash).lower()

        if flash not in ['master', 'slave']:
            return False

        json_data = {}
        json_data["Flash"] = flash
        if not self.post_to_bmc(self.bios_nextboot_uri, json_data):
            return False
        return True
