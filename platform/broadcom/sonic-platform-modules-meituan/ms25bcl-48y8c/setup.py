#!/usr/bin/env python

import os
import sys
from setuptools import setup
os.listdir

setup(
   name='sonic_platform',
   version='1.0',
   description='Module to initialize Celestica B3010 platforms',
      
   packages=['ms25bcl-48y8c', 'sonic_platform'],
   package_dir={'ms25bcl-48y8c'      : 'classes',
                'sonic_platform': 'sonic_platform'},
)

