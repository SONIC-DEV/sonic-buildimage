#!/bin/bash

# Add vlan
ip link add link eth0 name eth0.4088 type vlan id 4088
ip addr add 240.1.1.2/30 dev eth0.4088
ip link set eth0.4088 up
exit 0