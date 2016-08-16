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
"""Example, which shows the DCMs present in the network."""

import odl_utils.rest
import odl_utils.of
import odl_utils.output

# Read from file or full scan?
read_from_dump=False
dump_file='/afs/cern.ch/work/g/gjerecze/tmp/dcms'

# OpenDaylight
odl_host = 'pc-tbed-net-14'
# Look for DCMs on these hosts (hostname + OVSDB node name)
dcm_hosts = [{'hostname':'pc-tbed-r3-01','node':'pod-1-r3-01'},
             {'hostname':'pc-tbed-r3-02','node':'pod-1-r3-02'},
            ]

if not read_from_dump:
    r = odl_utils.rest.RestClient(odl_host)
    output = odl_utils.output.Output(silent = False)
    topo = odl_utils.of.Topology('of_topology', r)
    dcms = odl_utils.of.Dcm.scan_dcms(dcm_hosts, topo=topo, ignore_ip='10.193.170.24', dump_file=dump_file)
else:
    dcms = odl_utils.of.Dcm.load_dcms(dump_file)

i = 0
for dcm in dcms.values():
    dcm.show_info()
