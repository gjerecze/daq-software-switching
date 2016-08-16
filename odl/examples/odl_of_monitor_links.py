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
"""Example, which monitors status of the links in the network."""

import time
import odl_utils.rest
import odl_utils.of
import odl_utils.ovsdb

# OpenDaylight
odl_host = 'pc-tbed-net-14'
delay = 1
r = odl_utils.rest.RestClient(odl_host)
topo = odl_utils.of.Topology('of_topology', r)

while(1):
    print (time.ctime())
    topo.check_links()
    time.sleep(delay)
