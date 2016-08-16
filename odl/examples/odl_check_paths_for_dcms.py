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
"""Example, which shows path availability for DCMs in the network."""

import odl_utils.rest
import odl_utils.of
import odl_utils.output

# Check only this spine
use_spine = 'plane-2-spine-2'
# Look for DCMs on these hosts (hostname + OVSDB node name)
dcm_hosts = [{'hostname':'gklab-18-067','node':'hlt1-pod1'},
             {'hostname':'gklab-18-068','node':'hlt2-pod1'},
             {'hostname':'gklab-18-069','node':'hlt3-pod1'},
             {'hostname':'gklab-18-070','node':'hlt4-pod2'},
             {'hostname':'gklab-18-071','node':'hlt5-pod2'},
             {'hostname':'gklab-18-072','node':'hlt6-pod2'},
             ]

# OpenDaylight
odl_host = 'pc-tbed-net-14'

r = odl_utils.rest.RestClient(odl_host)
output = odl_utils.output.Output(silent = False)
topo = odl_utils.of.Topology('of_topology', r)

for dcm_host in dcm_hosts:
    # Scan for DCMs
    dcms = odl_utils.of.Dcm.scan_dcms(dcm_hosts, topo=topo, ignore_ip='10.193.170.24')
    for dcm in dcms.values():
        dcm.show_info()
        output.normal('Current path: {0}'.format(dcm.spine_in_use()))
        if dcm.is_path_up():
            output.okgreen('Available')
        else:
            output.fail('Not available')
        output.header('Requested path:')
        dcm.is_path_available(use_spine)
                
