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
"""Example, which shows how to distribute DCM flows acrros available paths in the network."""

import odl_utils.rest
import odl_utils.of
import odl_utils.output

# Read the DCM objects from this file (previously dumped with of.Dcm.scan_dcms.)
dump_file='/afs/cern.ch/work/g/gjerecze/tmp/dcms'

# Use daqring ports?
daqring=False

# OpenDaylight
odl_host = 'pc-tbed-net-14'

r = odl_utils.rest.RestClient(odl_host)
output = odl_utils.output.Output(silent = False)
topo = odl_utils.of.Topology('of_topology', r)
spines = topo.get_spines()
nspines = len(spines)
dcms = odl_utils.of.Dcm.load_dcms(dump_file)

i = 0
for dcm in dcms.values():
    print('')
    dcm.show_info()
    if nspines > 0:
        spine = spines[i % nspines]
    else:
        spine = None
    dcm.install_path(spine, usedaqring = daqring)
    i += 1
