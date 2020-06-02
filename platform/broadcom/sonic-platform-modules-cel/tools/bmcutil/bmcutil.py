#!/usr/bin/python

"""
bmcutil.py
BMC utility, implements management functions provided by BMC RESTful APIs.
"""

import requests
import re
import hashlib
import binascii
import os
import base64


# Base class of BmcUtil
class BmcUtilBase(object):
    def __init__(self):
        self.bmc_info_url = "http://240.1.1.1:8080/api/sys/bmc"
        self.bmc_eth_info_url = "http://240.1.1.1:8080/api/sys/eth"
        self.bmc_raw_command_url = "http://240.1.1.1:8080/api/sys/raw"
        self.bmc_pwd_url = "http://240.1.1.1:8080/api/sys/userpassword"
        self.bmc_pwd_path = "/usr/local/etc/bmcpwd"
        self.bmc_syslog_url = "http://240.1.1.1:8080/api/sys/syslog"

    def request_data(self, url):
        # Reqest data from BMC if not exist.
        data_req = requests.get(url)
        data_json = data_req.json()
        data_list = data_json.get('Information')
        return data_list

    def save_bmc_password(self, clear_pwd):
        enc = []
        key = "bmc"
        for i in range(len(clear_pwd)):
            key_c = key[i % len(key)]
            enc_c = chr((ord(clear_pwd[i]) + ord(key_c)) % 256)
            enc.append(enc_c)
        enc_pwd = base64.urlsafe_b64encode("".join(enc))

        with open(self.bmc_pwd_path, 'w') as file:
            file.write(enc_pwd)

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

    def version(self):
        """
            Return version information string
            @return version string of BMC OS
        """
        bmc_version = None

        bmc_version_key = "OpenBMC Version"
        bmc_info = self.request_data(self.bmc_info_url)
        bmc_version = bmc_info.get(bmc_version_key)

        return str(bmc_version)

    def set_eth0_addr(self, ip_addr, mask):
        """
            Set eth0 IPv4 address
            @ip_addr MANDATORY, IPv4 ip address string
            @mask MANDATORY, IPv4 network mask string
        """

        json_data = dict()
        json_data["data"] = "ifconfig eth0 %s netmask %s up" % (ip_addr, mask)
        r = requests.post(self.bmc_raw_command_url, json=json_data)
        if r.status_code != 200:
            return False
        return True

    def get_eth0_addr_list(self):
        """
            Get eth0 IPv4 address
            @return a list of (IPv4 ip address/mask string)
        """
        ipv4_adress = []
        eth_data_list = self.request_data(self.bmc_eth_info_url)

        for eth_data in eth_data_list:
            if 'inet addr' in eth_data:
                ipv4_list = re.findall(r'[0-9]+(?:\.[0-9]+){3}', eth_data)
                if len(ipv4_list) == 3:
                    ipv4 = ipv4_list[0] + "/" + ipv4_list[2]
                    ipv4_adress.append(ipv4)

        return str(ipv4_adress)

    def set_gateway_ip(self, gw_ip):
        """
            Set gateway IPv4 address string
            @gw_ip MANATORY, IPv4 address of gateway
        """

        json_data = dict()
        json_data["data"] = "route del default"

        r = requests.post(self.bmc_raw_command_url, json=json_data)
        if r.status_code != 200:
            return False

        json_data["data"] = "route add default gw %s" % gw_ip
        r = requests.post(self.bmc_raw_command_url, json=json_data)
        if r.status_code != 200:
            return False

        return True

    def get_gateway_ip(self):
        """
            Get gateway IPv4 address string
            @return IPv4 address of gateway
        """

        default_gw = None

        json_data = dict()
        json_data["data"] = "route"

        r = requests.post(self.bmc_raw_command_url, json=json_data)
        if r.status_code == 200:
            data_list = r.json().get('result')
            for raw_data in data_list:
                if 'default' in raw_data:
                    route_data = raw_data.split()
                    default_gw = route_data[1] if len(route_data) > 0 else None

        return str(default_gw)

    def set_user_and_passwd(self, user_name, password):
        """
            Set BMC user name and password
            @user_name MANDATORY, BMC user
            @password MANDATORY, BMC user's password
        """
        json_data = dict()
        json_data["user"] = str(user_name)
        json_data["oldpassword"] = self.get_bmc_pass()
        json_data["newpassword"] = password
        r = requests.post(self.bmc_pwd_url, json=json_data)
        return_data = r.json()

        if r.status_code != 200 or 'success' not in return_data.get('result'):
            return False

        self.save_bmc_password(password)
        return True

    def add_syslog_server(self, svr_ip, svr_port):
        """
            Add syslog server for BMC
            @svr_ip MANDATORY, syslog server IP string
            @svr_port MANDATORY, syslog server destination port
        """
        json_data = dict()
        json_data["addr"] = str(svr_ip)
        json_data["port"] = str(svr_port)
        r = requests.post(self.bmc_syslog_url, json=json_data)
        if r.status_code != 200 or 'success' not in r.json().get('result'):
            return False
        return True

    def get_syslog_server_list(self):
        """
            # Get syslog server list of BMC
            # @return a list of syslog server ip and destination port pair
        """
        syslog_ip = None
        syslog_port = None

        json_data = dict()
        json_data["data"] = "tail -n 1 /etc/rsyslog.conf"
        r = requests.post(self.bmc_raw_command_url, json=json_data)
        if r.status_code != 200:
            return False

        return_data = r.json()
        result = return_data.get("result")
        ip = re.findall(r'[0-9]+(?:\.[0-9]+){3}', result[0])
        port = str(result[0]).split(":")
        syslog_ip = ip[0] if len(ip) > 0 else None
        syslog_port = port[1] if len(port) > 1 else None

        return [syslog_ip, syslog_port]

    def del_syslog_server(self, svr_ip, svr_port):
        """ 
            Delete syslog server for BMC
            @svr_ip MANDATORY, syslog server IP string
            @svr_port MANDATORY, syslog server destination port
        """
        json_data = dict()
        json_data["addr"] = "127.0.0.1"
        json_data["port"] = str(svr_port)
        r = requests.post(self.bmc_syslog_url, json=json_data)
        if r.status_code != 200 or 'success' not in r.json().get('result'):
            return False
        return True

    def get_bmc_system_state(self):
        """
            Get BMC system state, includes CPU, memory, storage
            MUST contains status of: CPU, memory, disk
            dict object:
            {
                "CPU": {
                    "StateOutputs": "output of command 'top -bn 1'"
                    "Usage": "10.0"
                },
                "MEMORY": {
                    "StateOutputs": "output of command 'free -m'"
                    "Usage": "15.0"   # caculate: "free -t | grep \"buffers/cache\" | awk '{ printf \"mem usage  : %.1f%%\\n\",$3/($3+$4) * 100}'"
                },
                "DISK": {
                    "StateOutput": "output of command 'df -h'"
                    "Usage": "12.5"
                }
            }
        """

        state_data = dict()
        bmc_info = self.request_data(self.bmc_info_url)

        cpu_key = "CPU Usage"
        cpu_data_raw = bmc_info.get(cpu_key)
        cpu_usage = cpu_data_raw.split()[1].strip('%')
        cpu_data = dict()
        cpu_data["StateOutputs"] = "output of command 'top -bn 1'"
        cpu_data["Usage"] = "{:.1f}".format(float(cpu_usage))
        state_data["CPU"] = cpu_data

        disk_key = "Disk Usage"
        disk_data_raw = bmc_info.get(disk_key)
        disk_usage = disk_data_raw.split()[7].strip('%')
        disk_data = dict()
        disk_data["StateOutputs"] = "output of command 'df -h'"
        disk_data["Usage"] = "{:.1f}".format(float(disk_usage))
        state_data["DISK"] = disk_data

        json_data = dict()
        json_data["data"] = "free -t"
        mem_usage = "None"
        r = requests.post(self.bmc_raw_command_url, json=json_data)
        if r.status_code == 200:
            mem_data_raw = r.json().get('result')[2]
            mem_u = float(mem_data_raw.split()[2])
            mem_f = float(mem_data_raw.split()[3])
            mem_usage = (mem_u/(mem_u+mem_f)) * 100
        mem_data = dict()
        mem_data["StateOutputs"] = "output of command 'free -t'"
        mem_data["Usage"] = "{:.1f}".format(mem_usage)
        state_data["MEMORY"] = mem_data

        return state_data

    def reboot_bmc(self):
        """
            Reboot BMC
        """
        json_data = dict()
        json_data["data"] = "reboot"
        r = requests.post(self.bmc_raw_command_url, json=json_data)
        if r.status_code != 200:
            return False

        return True

    def set_location_led(self, admin_state):
        """
            Enable/disable location LED
            @admin_state MANDATORY, should be string "on" or "off"
        """

        json_data = dict()
        if str(admin_state).lower() not in ["on", "off"]:
            return False

        json_data["data"] = "led_location.sh %s" % admin_state
        r = requests.post(self.bmc_raw_command_url, json=json_data)
        if r.status_code != 200:
            return False

        return True
