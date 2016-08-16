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
"""Example, which shows all OVSDB nodes in the topology."""

import odl_utils.rest
import odl_utils.ovsdb
import odl_utils.output

# OpenDaylight
odl_host = 'pc-tbed-net-14'

r = odl_utils.rest.RestClient(odl_host)
output = odl_utils.output.Output(silent = False)

output.normal('Looking for ovsdb nodes...')

roses = {}
hlts = {}
plane1 = {}
plane2 = {}

nodes = odl_utils.ovsdb.OvsdbClient.get_all_nodes(r)
for node in nodes:
    name = node.name()
    if 'ros' in name:
        roses[name] = node
    if 'hlt' in name:
        hlts[name] = node
    if 'plane-1' in name:
        plane1[name] = node
    if 'plane-2' in name:
        plane2[name] = node

output.header('\n############ ROS ############')
for o in roses.values():
    output.header('\n{0}'.format(o.name()))
    if o.is_connected():
        output.okgreen('Status: connected')
    else:
        output.fail('Status: connected')
    if o.is_bridge():
        output.okgreen('Bridge: created')
        output.normal('OF ID: {0:d}'.format(o.of_id()))
    else:
        output.fail('Bridge: none')
    

output.header('\n############ HLT ############')
for o in hlts.values():
    output.header('\n{0}'.format(o.name()))
    if o.is_connected():
        output.okgreen('Status: connected')
    else:
        output.fail('Status: connected')
    if o.is_bridge():
        output.okgreen('Bridge: created')
        output.normal('OF ID: {0:d}'.format(o.of_id()))
    else:
        output.fail('Bridge: none')

output.header('\n###### LEAF-SPINE PLANE 1 ######')
for o in plane1.values():
    output.header('\n{0}'.format(o.name()))
    if o.is_connected():
        output.okgreen('Status: connected')
    else:
        output.fail('Status: connected')
    if o.is_bridge():
        output.okgreen('Bridge: created')
        output.normal('OF ID: {0:d}'.format(o.of_id()))
    else:
        output.fail('Bridge: none')

output.header('\n###### LEAF-SPINE PLANE 2 ######')
for o in plane2.values():
    output.header('\n{0}'.format(o.name()))
    if o.is_connected():
        output.okgreen('Status: connected')
    else:
        output.fail('Status: connected')
    if o.is_bridge():
        output.okgreen('Bridge: created')
        output.normal('OF ID: {0:d}'.format(o.of_id()))
    else:
        output.fail('Bridge: none')


