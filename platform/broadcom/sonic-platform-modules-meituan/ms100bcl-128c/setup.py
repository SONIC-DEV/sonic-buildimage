#!/usr/bin/env python

import os
import sys
from setuptools import setup
os.listdir

setup(
   name='sonic_platform',
   version='1.0',
   description='Module to initialize Celestica MT3010 platforms',
      
   packages=['ms100bcl-128c', 'sonic_platform'],
   package_dir={'ms100bcl-128c'      : 'classes',
                'sonic_platform': 'sonic_platform'},
)

