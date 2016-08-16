#!/usr/bin/env python
# © Copyright 2016 CERN
#
# This software is distributed under the terms of the GNU General Public 
# Licence version 3 (GPL Version 3), copied verbatim in the file "LICENSE".
#
# In applying this licence, CERN does not waive the privileges and immunities
# granted to it by virtue of its status as an Intergovernmental Organization 
# or submit itself to any jurisdiction.
#
# Author: Grzegorz Jereczek <grzegorz.jereczek@cern.ch>
#
"""Example, which initializes a network controlled with OpenDaylight."""

import odl_utils.rest
import odl_utils.ovsdb
import time
from sh import ssh

# OpenDaylight
odl_host = 'pc-tbed-net-14'
# IP address of the OpenFlow controller
ctrl_ip  = '10.193.16.29'
# Remove the entire topology first?
restart = True

# List of hosts where OVSDB server is running to include in the topology.
# Each entry includes the node name (convetion must be followed as required
# by odl_utils modules) and IP address of OVSDB server. ovsdbcl will be filled
# with the OvsdbClient object.

# Simple example with a back-back setup.
ovsdbs = {
        'pod-1-r3-01':{'ip':'10.193.4.16','ovsdbcl':None},
        'pod-1-r3-02':{'ip':'10.193.4.17','ovsdbcl':None},
        }
# Interface names to be added to the Bridge (if non-standard)
host_ports = ['eth6', 'eth7']

# Different example with full parallel leaf-spine topology.
#ovsdbs = {
#        'plane-1-pod-1':{'ip':'10.102.18.152','ovsdbcl':None},
#        'plane-1-pod-2':{'ip':'10.102.18.153','ovsdbcl':None},
#        'plane-1-spine-1':{'ip':'10.102.18.154','ovsdbcl':None},
#        'plane-1-spine-2':{'ip':'10.102.18.155','ovsdbcl':None},
#        'plane-2-pod-1':{'ip':'10.102.18.156','ovsdbcl':None},
#        'plane-2-pod-2':{'ip':'10.102.18.157','ovsdbcl':None},
#        'plane-2-spine-1':{'ip':'10.102.18.158','ovsdbcl':None},
#        'plane-2-spine-2':{'ip':'10.102.18.159','ovsdbcl':None},
#        'ros1-pod1':{'ip':'10.102.18.61','ovsdbcl':None},
#        'ros2-pod1':{'ip':'10.102.18.62','ovsdbcl':None},
#        'ros3-pod1':{'ip':'10.102.18.63','ovsdbcl':None},
#        'ros4-pod2':{'ip':'10.102.18.64','ovsdbcl':None},
#        'ros5-pod2':{'ip':'10.102.18.65','ovsdbcl':None},
#        'ros6-pod2':{'ip':'10.102.18.66','ovsdbcl':None},
#        'hlt1-pod1':{'ip':'10.102.18.67','ovsdbcl':None},
#        'hlt2-pod1':{'ip':'10.102.18.68','ovsdbcl':None},
#        'hlt3-pod1':{'ip':'10.102.18.69','ovsdbcl':None},
#        'hlt4-pod2':{'ip':'10.102.18.70','ovsdbcl':None},
#        'hlt5-pod2':{'ip':'10.102.18.71','ovsdbcl':None},
#        'hlt6-pod2':{'ip':'10.102.18.72','ovsdbcl':None},
#        }

r = odl_utils.rest.RestClient(odl_host, silent = True)

for ovsdb in ovsdbs:
    if 'plane' in ovsdb:
        dpdk = True
        # Count the number of DPDK ports
        nports = int(ssh("root@" + ovsdbs[ovsdb]['ip'],
                "/root/dpdk/tools/dpdk_nic_bind.py --status | grep drv=igb_uio | wc -l").stdout)
        ports = []
        for i in range(nports):
            ports.append('dpdk' + str(i))
    else:
        dpdk = False
        ports = host_ports

    print 'Initializing {0}, ports: {1}.'.format(ovsdb, ', '.join(map(str, ports)))
    ovsdbs[ovsdb]['ovsdbcl'] = odl_utils.ovsdb.OvsdbClient(ovsdb, r, ovsdbs[ovsdb]['ip'], isDpdk=dpdk, silent=False) 

    if restart:
        print 'Removing...'
        if ovsdbs[ovsdb]['ovsdbcl'].is_bridge():
            print 'Bridge found. Removing...'
            ovsdbs[ovsdb]['ovsdbcl'].remove_bridge()
            time.sleep(5)
        ovsdbs[ovsdb]['ovsdbcl'].remove_node()
        time.sleep(5)

    ovsdbs[ovsdb]['ovsdbcl'].connect()
    ovsdbs[ovsdb]['ovsdbcl'].create_bridge(ports, ctrl_ip, isDpdk=dpdk)

    print "Done\n"
