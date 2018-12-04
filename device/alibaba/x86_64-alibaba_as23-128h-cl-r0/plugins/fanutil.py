#!/usr/bin/env python

import re
import requests


class FanUtil():
    """Platform-specific FanUtil class"""

    def __init__(self):

        self.fan_fru_url = "http://240.1.1.1:8080/api/sys/fruid/fan"
        self.sensor_url = "http://240.1.1.1:8080/api/sys/sensors"
        self.fru_data_list = None
        self.sensor_data_list = None

    def request_data(self):
        # Reqest data from BMC if not exist.
        if self.fru_data_list is None or self.sensor_data_list is None:
            fru_data_req = requests.get(self.fan_fru_url)
            sensor_data_req = requests.get(self.sensor_url)
            fru_json = fru_data_req.json()
            sensor_json = sensor_data_req.json()
            self.fru_data_list = fru_json.get('Information')
            self.sensor_data_list = sensor_json.get('Information')
        return self.fru_data_list, self.sensor_data_list

    def name_to_index(self, fan_name):
        # Get fan index from fan name
        match = re.match(r"(FAN)([0-9]+)-(1|2)", fan_name, re.I)
        fan_index = None
        if match:
            i_list = list(match.groups())
            fan_index = int(i_list[1])*2 - (int(i_list[2]) % 2)
        return fan_index

    def get_num_fans(self):
        """   
            Get the number of fans
            :return: int num_fans
        """
        num_fans = 10

        return num_fans

    def get_fan_speed(self, fan_name):
        """
            Get the current speed of the fan, the unit is "RPM"  
            :return: int fan_speed
        """

        try:
            # Get real fan index
            index = self.name_to_index(fan_name)

            # Set key and index.
            fan_speed = 0
            position_key = "Front" if index % 2 != 0 else "Rear"
            index = int(round(float(index)/2))
            fan_key = "Fan " + str(index) + " " + position_key

            # Request and validate fan information.
            self.fru_data_list, self.sensor_data_list = self.request_data()

            # Get fan's speed.
            for sensor_data in self.sensor_data_list:
                sensor_name = sensor_data.get('name')
                if "fan" in str(sensor_name):
                    fan_data = sensor_data.get(fan_key)
                    fan_sp_list = map(int, re.findall(r'\d+', fan_data))
                    fan_speed = fan_sp_list[0]

        except:
            return 0

        return fan_speed

    def get_fan_low_threshold(self, fan_name):
        """
            Get the low speed threshold of the fan.
            if the current speed < low speed threshold, 
            the status of the fan is not ok.
            :return: int fan_low_threshold
        """

        try:
            # Get real fan index
            index = self.name_to_index(fan_name)

            # Set key and index.
            fan_low_threshold = 0
            position_key = "Front" if index % 2 != 0 else "Rear"
            index = int(round(float(index)/2))
            fan_key = "Fan " + str(index) + " " + position_key

            # Request and validate fan information.
            self.fru_data_list, self.sensor_data_list = self.request_data()

            # Get fan's threshold.
            for sensor_data in self.sensor_data_list:
                sensor_name = sensor_data.get('name')
                if "fan" in str(sensor_name):
                    fan_data = sensor_data.get(fan_key)
                    fan_sp_list = map(int, re.findall(r'\d+', fan_data))
                    fan_low_threshold = fan_sp_list[1]

        except:
            return "N/A"

        return fan_low_threshold

    def get_fan_high_threshold(self, fan_name):
        """
            Get the hight speed threshold of the fan, 
            if the current speed > high speed threshold, 
            the status of the fan is not ok
            :return: int fan_high_threshold 
        """

        try:
            # Get real fan index
            index = self.name_to_index(fan_name)

            # Set key and index.
            fan_high_threshold = 0
            position_key = "Front" if index % 2 != 0 else "Rear"
            index = int(round(float(index)/2))
            fan_key = "Fan " + str(index) + " " + position_key

            # Request and validate fan information.
            self.fru_data_list, self.sensor_data_list = self.request_data()

            # Get fan's threshold.
            for sensor_data in self.sensor_data_list:
                sensor_name = sensor_data.get('name')
                if "fan" in str(sensor_name):
                    fan_data = sensor_data.get(fan_key)
                    fan_sp_list = map(int, re.findall(r'\d+', fan_data))
                    fan_high_threshold = fan_sp_list[2]

        except:
            return 0

        return fan_high_threshold

    def get_fan_pn(self, fan_name):
        """
            Get the product name of the fan
            :return: str fan_pn
        """

        try:
            # Get real fan index
            index = self.name_to_index(fan_name)

            # Set key and index.
            fan_pn = "N/A"
            index = int(round(float(index)/2))
            fan_fru_key = "Fantray" + str(index)

            # Request and validate fan information.
            self.fru_data_list, self.sensor_data_list = self.request_data()

            # Get fan's fru.
            for fan_fru in self.fru_data_list:
                matching_fan = [s for s in fan_fru if fan_fru_key in s]
                if matching_fan:
                    pn = [s for s in fan_fru if "Part" in s]
                    fan_pn = pn[0].split()[4]

        except:
            return "N/A"

        return fan_pn

    def get_fan_sn(self, fan_name):
        """
            Get the serial number of the fan
            :return: str fan_sn
        """
        try:
            # Get real fan index
            index = self.name_to_index(fan_name)

            # Set key and index.
            fan_sn = "N/A"
            index = int(round(float(index)/2))
            fan_fru_key = "Fantray" + str(index)

            # Request and validate fan information.
            self.fru_data_list, self.sensor_data_list = self.request_data()

            # Get fan's fru.
            for fan_fru in self.fru_data_list:
                matching_fan = [s for s in fan_fru if fan_fru_key in s]
                if matching_fan:
                    serial = [s for s in fan_fru if "Serial" in s]
                    fan_sn = serial[0].split()[3]

        except:
            return "N/A"

        return fan_sn

    def get_fans_name_list(self):
        """
            Get list of fan name.
            :return: list fan_names
        """
        fan_names = []

        # Get the number of fans
        n_fan = self.get_num_fans()

        # Set fan name and add to the list.
        for x in range(1, n_fan + 1):
            f_index = int(round(float(x)/2))
            pos = 1 if x % 2 else 2
            fan_name = 'FAN{}-{}'.format(f_index, pos)
            fan_names.append(fan_name)

        return fan_names

    def get_all(self):
        """
            Get all information of system FANs, returns JSON objects in python 'DICT'.
            Number, mandatory, max number of FAN, integer
            FAN1_1, FAN1_2, ... mandatory, FAN name, string
            Present, mandatory for each FAN, present status, boolean, True for present, False for NOT present, read directly from h/w
            Running, conditional, if PRESENT is True, running status of the FAN, True for running, False for stopped, read directly from h/w
            Speed, conditional, if PRESENT is True, real FAN speed, float, read directly from h/w
            LowThd, conditional, if PRESENT is True, lower bound of FAN speed, float, read from h/w
            HighThd, conditional, if PRESENT is True, upper bound of FAN speed, float, read from h/w
            PN, conditional, if PRESENT is True, PN of the FAN, string
            SN, conditional, if PRESENT is True, SN of the FAN, string)
        """

        self.fru_data_list, self.sensor_data_list = self.request_data()
        all_fan_dict = dict()

        # Get the number of fans
        n_fan = self.get_num_fans()
        all_fan_dict["Number"] = n_fan

        # Set fan FRU data.
        fan_fru_dict = dict()
        for fan_fru in self.fru_data_list:
            if len(fan_fru) == 0:
                continue
            fru_dict = dict()
            fan_key = fan_fru[0].split()
            fan_ps = False

            if str(fan_key[-1]).lower() == "absent":
                fan_idx = int(re.findall('\d+', fan_key[0])[0])
            else:
                fan_idx = int(re.findall('\d+', fan_key[-1])[0])
                fan_ps = True
                pn = [s for s in fan_fru if "Part" in s]
                sn = [s for s in fan_fru if "Serial" in s]
                fan_pn = pn[0].split(":")[-1].strip() if len(pn) > 0 else 'N/A'
                fan_sn = sn[0].split(":")[-1].strip() if len(sn) > 0 else 'N/A'

            fru_dict["PN"] = "N/A" if not fan_pn or fan_pn == "" else fan_pn
            fru_dict["SN"] = "N/A" if not fan_sn or fan_sn == "" else fan_sn
            fru_dict["Present"] = fan_ps
            fan_fru_dict[fan_idx] = fru_dict

        # Set fan sensor data.
        for sensor_data in self.sensor_data_list:
            sensor_name = sensor_data.get('name')
            if "fan" in str(sensor_name):
                for x in range(1, n_fan + 1):
                    fan_dict = dict()
                    f_index = int(round(float(x)/2))
                    pos = 1 if x % 2 else 2
                    position_key = "Front" if x % 2 != 0 else "Rear"
                    fan_key = "Fan " + str(f_index) + " " + position_key
                    fan_data = sensor_data.get(fan_key)
                    fan_sp_list = map(int, re.findall(r'\d+', fan_data))
                    fan_dict["Present"] = fan_fru_dict[f_index]["Present"]
                    if fan_dict["Present"]:
                        fan_dict["Speed"] = fan_sp_list[0]
                        fan_dict["Running"] = True if fan_dict["Speed"] > 0 else False
                        fan_dict["LowThd"] = fan_sp_list[1]
                        fan_dict["HighThd"] = fan_sp_list[2]
                        fan_dict["PN"] = fan_fru_dict[f_index]["PN"]
                        fan_dict["SN"] = fan_fru_dict[f_index]["SN"]
                    fan_name = 'FAN{}-{}'.format(f_index, pos)
                    all_fan_dict[fan_name] = fan_dict
                break

        return all_fan_dict