#!/usr/bin/env python

import requests

class SensorUtil():
    """Platform-specific SensorUtil class"""

    def __init__(self):
        self.sensor_url = "http://[fe80::1:1%eth0.4088]:8080/api/sys/sensors"
        self.sensor_info_list = None


    def request_data(self):
        # Reqest data from BMC if not exist.
        if self.sensor_info_list is None:
            sensor_data_req = requests.get(self.sensor_url)
            sensor_json = sensor_data_req.json()
            self.sensor_info_list = sensor_json.get('Information')
        return self.sensor_info_list


    def input_type_selector(self, unit):
        # Set input type.
        return {
            "C"     : "temperature",
            "V"     : "voltage",
            "RPM"   : "fan_speed",
            "A"     : "current",
            "W"     : "power"
        }.get(unit, unit)  


    def get_num_sensors(self):     
        """   
            Get the number of sensors
            :return: int num_sensors
        """
        
        num_sensors = 0
        try:
            # Request and validate sensor's information
            self.sensor_info_list = self.request_data()
            
            # Get number of sensors. 
            num_sensors = len(self.sensor_info_list)
        except:
            print "Error: Unable to access sensor information"
            return 0

        return num_sensors


    def get_sensor_input_num(self, index):     
        """   
            Get the number of the input items of the specified sensor
            :return: int input_num
        """

        input_num = 0
        try:
            # Request and validate sensor's information.
            self.sensor_info_list = self.request_data()

            # Get sensor's input number.
            sensor_data = self.sensor_info_list[index-1]
            input_num = len(sensor_data.keys())-2
        except:
            print "Error: Unable to access sensor information"
            return 0
            
        return input_num


    def get_sensor_name(self, index):
        """   
            Get the device name of the specified sensor.
            for example "coretemp-isa-0000"
            :return: str sensor_name
        """

        sensor_name = "N/A"
        try:
            # Request and validate sensor's information.
            self.sensor_info_list = self.request_data()

            # Get sensor's name.
            sensor_data = self.sensor_info_list[index-1]
            sensor_name = sensor_data.get('name')

        except:
            return "N/A"

        return sensor_name


    def get_sensor_input_name(self, sensor_index, input_index):
        """
            Get the input item name of the specified input item of the 
            specified sensor index, for example "Physical id 0"
            :return: str sensor_input_name
        """

        sensor_input_name = "N/A"
        try:
            # Request and validate sensor's information.
            self.sensor_info_list = self.request_data()
            sensor_data = self.sensor_info_list[sensor_index-1].copy()

            # Remove none input key.
            del sensor_data["name"]
            del sensor_data["Adapter"]

            # Get sensor's input name.
            sensor_data_key = sensor_data.keys()
            sensor_input_name = sensor_data_key[input_index-1]
        except:
            return "N/A"

        return sensor_input_name


    def get_sensor_input_type(self, sensor_index, input_index):
        """
            Get the item type of the specified input item of the specified sensor index, 
            The return value should among  "valtage","temperature"
            :return: str sensor_input_type
        """

        sensor_input_type = "N/A"
        try:
            # Request and validate sensor's information.
            self.sensor_info_list = self.request_data()
            sensor_data = self.sensor_info_list[sensor_index-1].copy()

            # Remove none input key.
            del sensor_data["name"]
            del sensor_data["Adapter"]

            # Get sensor's input type name.
            sensor_data_key = sensor_data.keys()
            sensor_input_raw = sensor_data.get(sensor_data_key[input_index-1])
            sensor_data_str = sensor_input_raw.split()
            sensor_input_type = self.input_type_selector(sensor_data_str[1])
        except:
            return "N/A"

        return sensor_input_type


    def get_sensor_input_value(self, sensor_index, input_index):
        """
            Get the current value of the input item, the unit is "V" or "C"
            :return: float sensor_input_value
        """

        sensor_input_value = 0
        try:
            # Request and validate sensor's information.
            self.sensor_info_list = self.request_data()
            sensor_data = self.sensor_info_list[sensor_index-1].copy()

            # Remove none input key.
            del sensor_data["name"]
            del sensor_data["Adapter"]

            # Get sensor's input value.
            sensor_data_key = sensor_data.keys()
            sensor_input_raw = sensor_data.get(sensor_data_key[input_index-1])
            sensor_data_str = sensor_input_raw.split()
            sensor_input_value = float(sensor_data_str[0]) if sensor_data_str[0] != "N/A" else 0 
        except:
            print "Error: Unable to access sensor information"
            return 0

        return sensor_input_value


    def get_sensor_input_low_threshold(self, sensor_index, input_index):
        """
            Get the low threshold of the value, 
            the status of this item is not ok if the current value<low_threshold
            :return: float sensor_input_low_threshold
        """

        sensor_input_low_threshold = 0
        try:
            # Request and validate sensor's information.
            self.sensor_info_list = self.request_data()
            sensor_data = self.sensor_info_list[sensor_index-1].copy()

            # Remove none input key.
            del sensor_data["name"]
            del sensor_data["Adapter"]

            # Get sensor's input low threshold.
            sensor_data_key = sensor_data.keys()
            sensor_input_raw = sensor_data.get(sensor_data_key[input_index-1])
            sensor_data_str = sensor_input_raw.split()
            indices = [i for i, s in enumerate(sensor_data_str) if 'min' in s or 'low' in s]
            sensor_input_low_threshold = float(sensor_data_str[indices[0] + 2]) if len(indices) != 0 else 0

        except:
            print "Error: Unable to access sensor information"
            return 0

        return sensor_input_low_threshold


    def get_sensor_input_high_threshold(self, sensor_index, input_index):
        """
            Get the high threshold of the value, 
            the status of this item is not ok if the current value > high_threshold
            :return: float sensor_input_high_threshold
        """

        sensor_input_high_threshold = 0
        try:
            # Request and validate sensor's information.
            self.sensor_info_list = self.request_data()
            sensor_data = self.sensor_info_list[sensor_index-1].copy()

            # Remove none input key.
            del sensor_data["name"]
            del sensor_data["Adapter"]

            # Get sensor's input high threshold.            
            sensor_data_key = sensor_data.keys()
            sensor_input_raw = sensor_data.get(sensor_data_key[input_index-1])
            sensor_data_str = sensor_input_raw.split()
            indices = [i for i, s in enumerate(sensor_data_str) if 'max' in s or 'high' in s]
            sensor_input_high_threshold = float(sensor_data_str[indices[0] + 2]) if len(indices) != 0 else 0

        except:
            print "Error: Unable to access sensor information"
            return 0

        return sensor_input_high_threshold