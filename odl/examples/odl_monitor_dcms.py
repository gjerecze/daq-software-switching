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
"""Example, which monitors path availability for DCMs and triggers redistribution, if needed
This is just a starter. Not optimal!"""

import odl_utils.rest
import odl_utils.of
import odl_utils.output
import time

# OpenDaylight
odl_host = 'gklab-18-150'
# Look for DCMs on these hosts (hostname + OVSDB node name)
dcm_hosts = [{'hostname':'gklab-18-067','node':'hlt1-pod1'},
             {'hostname':'gklab-18-068','node':'hlt2-pod1'},
             {'hostname':'gklab-18-069','node':'hlt3-pod1'},
             {'hostname':'gklab-18-070','node':'hlt4-pod2'},
             {'hostname':'gklab-18-071','node':'hlt5-pod2'},
             {'hostname':'gklab-18-072','node':'hlt6-pod2'},
             ]

r = odl_utils.rest.RestClient(odl_host)
output = odl_utils.output.Output(silent = False)
topo = odl_utils.of.Topology('of_topology', r)

spines = topo.get_spines()
#spines = ['plane-1-spine-2', 'plane-1-spine-1', 'plane-2-spine-2', 'plane-2-spine-1']
nspines = len(spines)
redistribute = True
redistribute2 = False
rescan = True
all_up = True
dcms = []

while 1:

    if rescan:
        output.normal('Removing paths...')
        for dcm_name in sorted(dcms):
            dcm = dcms[dcm_name]
            dcm.remove_path()
        output.normal('Done.')
        # This step is required, because DCMs initate new TCP connections after link failure.
        #todo Assigning higher priority flows for new dcm objects, then remove old dcms with lower prioriy, and so on
        #todo Also, add default path failover in a similar fashion!!!
        output.header('Scanning dcms...')
        dcms = odl_utils.of.Dcm.scan_dcms(dcm_hosts, topo=topo)
        output.header('Done.\n')
        rescan = False
        continue

    if redistribute:
        output.header('Redistributing...')
        i = 0
        #todo base ring port number is hard-coded in of.py and ovsdb.py
        ring = 100
        for dcm_name in sorted(dcms):
            output.okblue('\n{0}'.format(dcm_name))
            dcm = dcms[dcm_name]
            #dcm.show_info()
            for j in range(nspines):
                spine = spines[i % nspines]
                if dcm.is_path_available(spine):
                    break
                else:
                    i += 1
            dcm.install_path(spine, usedaqring = True, daqring_id = ring)
            i += 1
            ring += 1
        output.header('Done.\n')
        redistribute = False

    if topo.check_links(silent = True):
        if all_up == False:
            redistribute = True
            rescan = True
            all_up = True
            output.okgreen('All links UP again.\n')
    else:
        if all_up:
            redistribute = True
            rescan = True
            all_up = False
            output.fail('Some link is DOWN.\n')
