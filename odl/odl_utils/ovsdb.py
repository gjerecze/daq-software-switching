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

import output
import rest
import time

class OvsdbClient(object):
    """Represents a connection to an OVSDB server. Only a single Bridge can be associted with a single
    OvsdbClient object."""

    @classmethod
    def get_all_nodes(cls, r, silent=True): 
        """Returns all OVSDB servers present in the configuration datastore.

        Args:
            r: RestClient object to access OpenDaylight's northbound REST interface.
            silent: Supresses output, if False.

        Returns:
            A list of Ovsdb objects.
        """
        ovsdbs = []
        nodes = []
        try:
            nodes = r.conf_ds_get('network-topology:network-topology/topology/ovsdb:1/')['topology'][0]['node']
        except (rest.RestError, KeyError):
            pass
        for node in nodes:
            if 'bridge' in node['node-id']:
                continue
            name = node['node-id'].replace('ovsdb://','')
            ovsdbs.append(OvsdbClient(name, r))
        return ovsdbs

    def __init__(self, name, rest_client, ip=None, port=16640, isDpdk=True, silent=True):
        """Creates a new OvsdbClient object.
        
        Args:
            name: Name of the this OVSDB node. Must follow the convention defined in the of module.
            rest_client: RestClient object to access OpenDaylight's northbound REST interface.
            ip: IP address of this OVSDB server.
            port: TCP port of this OVSDB server.
            isDPDK: If True, DPDK acceleration will be used for the bridges to be created with this server.
            silent: Supresses output, if False.

        Returns:
            An instance of the OvsdbClient class.
        """
        self.__o = output.Output(silent)

        self.__r = rest_client
        self.__id = name
        self.__ovsdb_id = 'ovsdb://' + name
        self.__ip = ip
        self.__port = port

        self.__ovsdb_oper_path = None
        self.__ovsdb_oper_id = None
        
        self.__node = {'network-topology:node':[{'node-id': self.__ovsdb_id}]}

        self.__bridge_path = None
        self.__bridge_id = None
        self.__of_id = None

        self.__ovsdb_path = \
            'network-topology:network-topology/topology/ovsdb:1/node/' + self.__ovsdb_id.replace('/', '%2F')

        # Check, if node of this name already exists in the datastore.
        try:
            self.__node['network-topology:node'] = self.__r.conf_ds_get(self.__ovsdb_path)['node']
            self.__ip = self.__node['network-topology:node'][0]['ovsdb:connection-info']['remote-ip']
            self.__port = self.__node['network-topology:node'][0]['ovsdb:connection-info']['remote-port']
        except (rest.RestError, KeyError):
            pass

        # Check, if connection is already active.
        if (self.__ip):
            self.__node['network-topology:node'][0]['connection-info'] = {
                    'ovsdb:remote-port':port,
                    'ovsdb:remote-ip':ip }

        # Initialize DPDK configuration, if requested.
        if isDpdk:
            self.__node['network-topology:node'][0]['ovsdb:openvswitch-other-configs'] = [
                {'ovsdb:other-config-key':'n-dpdk-rxqs',
                 'ovsdb:other-config-value':8},
                {'ovsdb:other-config-key':'pmd-cpu-mask',
                 'ovsdb:other-config-value':'0xfffe'}]

        self.__retries = 10
        self.__sleep_time = 10

    def name(self):
        """Returns the name of this OVSDB node."""
        return self.__id

    def of_id(self):
        """Returns of OpenFlow ID of the Bridge associated with this OVSDB node."""
        return self.__of_id

    def ip(self):
        """Returns the IP address of this OVSDB node."""
        return self.__ip

    def is_connected(self):
        """Returns True, if connection to this OVSDB node is active."""
        if not self.__ovsdb_oper_path:
            self.__o.normal('Looking for node in operational datastore...')
            try:
                nodes = self.__r.oper_ds_get('network-topology:network-topology/topology/ovsdb:1/')['topology'][0]['node']
            except (rest.RestError, KeyError):
                return False
            for node in nodes:
                if 'bridge' in node['node-id']:
                    continue
                if self.__ip == node['ovsdb:connection-info']['remote-ip']:
                    self.__ovsdb_oper_id = node['node-id']
                    self.__ovsdb_oper_path = 'network-topology:network-topology/topology/ovsdb:1/node/' \
                                + self.__ovsdb_oper_id.replace('/', '%2F')
                    break
            if not self.__ovsdb_oper_path:
                self.__o.normal('Not found.')
                return False
            else:
                self.__o.normal('Found: node-id {0}'.format(node['node-id']))
        try:
            node = self.__r.oper_ds_get(self.__ovsdb_oper_path)['node'][0]
            return node['ovsdb:manager-entry'][0]['connected']
        except rest.RestError:
            return False
        return False 

    def is_bridge(self):
        """Returns True, if a Bridge has been already initialized on this OVSDB node."""
        if not self.is_connected():
            return False
        self.__bridge_id = self.__ovsdb_oper_id + '/bridge/br0'
        self.__bridge_path = \
            'network-topology:network-topology/topology/ovsdb:1/node/' + self.__bridge_id.replace('/', '%2F')
        try:
            node = self.__r.oper_ds_get(self.__bridge_path)['node'][0]
            self.__of_id = int(node['ovsdb:datapath-id'].replace(':',''), 16)
        except (rest.RestError, KeyError):
            return False
        return True

    def remove_bridge(self):
        """Removes bridge from the configuration datastore."""
        if self.__bridge_path:
            try:
                self.__r.conf_ds_delete(self.__bridge_path)
            except rest.RestError:
                pass
            self.__bridge_path = None
            self.__bridge_id = None

    def remove_node(self):
        """Removes this OVSDB node from the configuration datastore."""
        self.remove_bridge()
        try:
            self.__r.conf_ds_delete(self.__ovsdb_path)
        except rest.RestError:
            pass

    def connect(self):
        """Connects to this OVSDB server."""
        self.__o.header('Connecting to ovsdb-server {0} at {1}:{2:d}...'.format(self.__id, self.__ip, self.__port))

        try:
            node = self.__r.conf_ds_get(self.__ovsdb_path)['node'][0]
        except rest.RestError:
            self.__o.normal('Inserting node...')
            self.__r.conf_ds_put(self.__ovsdb_path, self.__node)
            retries = self.__retries
        else:
            self.__o.normal('Node exists.')
            retries = 1

        self.__o.normal('Verifying connection...')
        while retries:
            if (self.is_connected()):
                self.__o.okgreen('Connected.')
                return
            time.sleep(self.__sleep_time)
            retries -= 1

        # Still not connected. Try resetting the connection
        self.__o.warn('Connection failed. Reconnecting...')
        self.remove_node()
        time.sleep(self.__sleep_time)
        self.__r.conf_ds_put(self.__ovsdb_path, self.__node)

        self.__o.normal('Verifying connection...')
        retries = self.__retries
        while retries:
            time.sleep(self.__sleep_time)
            if (self.is_connected()):
                break
            retries -= 1
        if retries:
            self.__o.okgreen('Connected.')
        else:
            self.__o.fail('Cannot connect')

    def create_bridge(self, port_names, ctrl_ip, ctrl_port = 6653, isDpdk=True, nDaqrPorts=64):
        """Creates a Bridge (br0) on this OVSDB node.

        Args:
            port_names: a list of port names to add to the Bridge.
            ctrl_ip: IP address of the OpenFlow controller to connect to.
            ctrl_port: TCP port of the OpenFlow controller to connect to.
            isDPDK: If True, DPDK acceleration will be used for this Bridge.
            nDaqrPorts: The number of daqring ports to create on the Bridge (isDPDK must be True).

        Returns:
            None
        """
            
        self.__o.header('Creating bridge at {0}...'.format(self.__id))

        self.__o.normal('Verifying, if bridge exists...')
        if (self.is_bridge()):
            self.__o.warn('Bridge already exists.')
        self.__o.normal('Creating bridge...')

        if isDpdk:
            dp_type = 'ovsdb:datapath-type-netdev'
        else:
            dp_type = 'ovsdb:datapath-type-system'

        self.__bridge_id = self.__ovsdb_oper_id + '/bridge/br0'
        self.__bridge_path = \
            'network-topology:network-topology/topology/ovsdb:1/node/' + self.__bridge_id.replace('/', '%2F')

        bridge = {'node':[{
                  'node-id':self.__bridge_id,
                  'ovsdb:bridge-name':'br0',
                  'ovsdb:protocol-entry':[{'protocol':'ovsdb:ovsdb-bridge-protocol-openflow-10'}],
                  'ovsdb:controller-entry':[{'target':'tcp:' + ctrl_ip + ':' + str(ctrl_port)}],
                  'ovsdb:bridge-other-configs':[{'bridge-other-config-key':'disable-in-band',
                                                 'bridge-other-config-value':'true'}],
                  'ovsdb:datapath-type':dp_type,
                  'termination-point': [],
                  'ovsdb:managed-by':'/network-topology:network-topology/network-topology:topology[network-topology:topology-id=\'ovsdb:1\']/network-topology:node[network-topology:node-id=\'' + self.__ovsdb_oper_id + '\']'
                  }]}

        nport = 0
        for port in port_names:
            nport += 1
            if 'dpdk' in port:
                if_type = 'ovsdb:interface-type-dpdk'
            else:
                if_type = 'ovsdb:interface-type-system'
            node = {'tp-id':port,
                    'ovsdb:name':port,
                    'ovsdb:ofport':str(nport),
                    'ovsdb:interface-type':if_type}
            bridge['node'][0]['termination-point'].append(node)

        if isDpdk:
            #todo base port number for the daqring ports is hard-coded and split into two sets (for leafs and spines).
            nport = 0
            base = 100
            for i in range(nDaqrPorts):
                of_port = base + nport
                name = 'dpdkdaqr' + str(of_port)
                if_type = 'ovsdb:interface-type-dpdkdaqr'
                node = {'tp-id':name,
                        'ovsdb:name':name,
                        'ovsdb:ofport':str(of_port),
                        'ovsdb:interface-type':if_type}
                bridge['node'][0]['termination-point'].append(node)
                nport += 1
            nport = 0
            base = 200
            for i in range(nDaqrPorts):
                of_port = base + nport
                name = 'dpdkdaqr' + str(of_port)
                if_type = 'ovsdb:interface-type-dpdkdaqr'
                node = {'tp-id':name,
                        'ovsdb:name':name,
                        'ovsdb:ofport':str(of_port),
                        'ovsdb:interface-type':if_type}
                bridge['node'][0]['termination-point'].append(node)
                nport += 1

        self.__r.conf_ds_put(self.__bridge_path, bridge)
        self.__o.normal('Verifying...')
        retries = self.__retries
        while retries:
            if (self.is_bridge()):
                self.__o.okgreen('Created. OpenFlow ID: {0}'.format(self.__of_id))
                return
            time.sleep(self.__sleep_time)
            retries -= 1
