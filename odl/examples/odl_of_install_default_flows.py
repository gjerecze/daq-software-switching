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
"""Example, which install default paths through the network. Only one spine switch is used."""

import odl_utils.rest
import odl_utils.of
import odl_utils.ovsdb
import odl_utils.output

# OpenDaylight
odl_host = 'pc-tbed-net-14'

r = odl_utils.rest.RestClient(odl_host, silent=True)
output = odl_utils.output.Output()

topo = odl_utils.of.Topology('of_topology', r)
topo.clear_flows()
topo.set_drop_flows()
topo.set_default_flows()
topo.set_lldp_flows()
#topo.set_dhcp_paths()
