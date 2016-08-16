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
""" Example, which removes all flows beloning to DCMs from all bridges."""

import odl_utils.rest
import odl_utils.of
import odl_utils.output

# OpenDaylight
odl_host = 'gklab-18-150'
# Look for DCMs on these hosts (hostname + OVSDB node name)
dcm_hosts = [{'hostname':'gklab-18-067','node':'hlt1-pod1'},
             {'hostname':'gklab-18-068','node':'hlt2-pod1'},
             {'hostname':'gklab-18-069','node':'hlt3-pod1'},
             {'hostname':'gklab-18-070','node':'hlt4-pod2'},
             {'hostname':'gklab-18-071','node':'hlt5-pod2'},
             {'hostname':'gklab-18-072','node':'hlt6-pod2'}]

r = odl_utils.rest.RestClient(odl_host)
output = odl_utils.output.Output(silent = False)
topo = odl_utils.of.Topology('of_topology', r)
dcms = odl_utils.of.Dcm.scan_dcms(dcm_hosts, topo=topo)

output.header('Removing DCM paths...')
for dcm in dcms.values():
    dcm.show_info()
    dcm.remove_path()
output.header('Done.')

