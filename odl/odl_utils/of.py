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
"""
OpenFlow helpers for TCP-based DAQ networks on the example of the ATLAS DAQ network
and parallel leaf-spine topology.
(c) Copyright 2016 Grzegorz Jereczek. All Rights Reserved.
"""

import output
import rest
import ovsdb
import time
import re
import sh
import cPickle as pickle
from sh import ssh

class Dcm(object):
    """Represents a Data Collection Manager."""

    @staticmethod
    def scan_dcms(hosts_list, topo=None, dst_network='10.193.160.0/19', ignore_ip=None, dump_file=None):
        """Returns a dictionary of all Dcms in the topology.

        SSH is used to get a list of TCP connections on every end-node
        in the topology. This list is filtered Dcm by Dcm to get all incoming
        TCP flows.

        Args:
            hosts_list: A list of dictonaries containing 'hostname' with host to be scanned,
                        and 'node' containing the name of the Bridge running on that host
                        (or the last hop).
            topo: A Topology object.
            dst_network: A string with the network carrying the Dcm flows.
            ignore_ip: Ignores TCP flows from this IP.
            dump_file: For dumping a dict with Dcms.

        Returns:
            A dict with Dcm objects.
        """

        dcms = {}
        i = 0
        for h in hosts_list:
            host = h['hostname']
            node = h['node']
            tcp_flows = ssh(host, "/usr/sbin/ss -p dst " + dst_network).stdout.strip()
            try:
                # Get all distinct Dcm processes
                dcm_pids = ssh(host, "ps -C dcm_main -o pid --sort -pcpu --no-headers").stdout.split()
            except sh.ErrorReturnCode_1:
                continue
            ndcm = 0
            for p in dcm_pids:
                ndcm += 1
                i += 1
                inflows = []
                ippat = '\d+\.\d+\.\d+\.\d+'
                # regex pattern to get the TCP flows (4-tuple) of a particular Dcm process
                pattern = '\S+\s+\S+\s+\S+\s+('+ippat+'):(\d*)\s*('+ippat+'):(\d*).*\"dcm_main\",' + str(p) + '.*'
                r = re.compile(pattern)
                for m in re.finditer(r, tcp_flows):
                    if ignore_ip == m.group(3):
                        continue
                    # incoming Dcm flows: (dport, sip, sport)
                    inflows.append( (int(m.group(2)), m.group(3), int(m.group(4))) )
                    # dip is the same for all flows
                    dip = m.group(1)
                dcms[host + '-' + str(ndcm)] = Dcm(i, topo, node, dip, inflows, int(p)) 

        if dump_file:
            with open(dump_file, 'wb') as output:
                pickler = pickle.Pickler(output, -1)
                pickler.dump(dcms)

        return dcms

    @staticmethod
    def load_dcms(file):
        """Loads the previously dumped Dcms from a file.

        Args:
            file: Full path to the file containing the Dcm objects.

        Returns:
            A dict with Dcm objects.
        """
        dcms = {}
        with open(file, 'rb') as input:
            dcms = pickle.load(input)
        return dcms


    def __init__(self, dcm_id, topo, node, dip, inflows, pid, silent = False):
        """Creates a new Dcm object.

        Args:
            dcm_id: Unique numerical ID of a Dcm.
            topo: The Topology, to which the Dcm belongs.
            node: The name of the Bridge running on the same host
                  as this Dcm or the last Bridge before this host.
            dip: This Dcm's IP.
            inflows: List of TCP flows coming into this Dcm as a tuple
                     (dport, sip, sport)
            pid: This Dcm's process ID.
            silent: Suppress the output, if True.

        Returns:
            An instance of the Dcm class.
        """
        self.__o = output.Output(silent)
        self.__id = dcm_id
        self.__topo = topo
        #todo add validation for node naming
        self.__node = node
        self.__dip = dip
        self.__inflows = inflows
        self.__pid = pid

    def get_dcm_id(self):
        """Returns this Dcm's id."""
        return self.__id

    def get_node(self):
        """Returns the name of the last Bridge nearest to this Dcm."""
        return self.__node

    def get_pod_id(self):
        """Returns the ID of the pod, in which this Dcm is located."""
        pattern = 'pod-(\d+)-.*'
        r = re.compile(pattern)
        m = r.match(self.__node)
        return m.group(1)

    def get_ip(self):
        """Returns this Dcm's IP address."""
        return self.__dip

    def get_inflows(self):
        """Returns a list of tuples specifying TCP flows belonging to this DCM.""" 
        return self.__inflows

    def __get_pod_id(self):
        """Returns the ID of the pod, in which this Dcm is located."""
        pattern = 'pod-(\d+)-.*'
        r = re.compile(pattern)
        m = r.match(self.__node)
        return int(m.group(1))

    def __get_plane_id(self, spine):
        """Returns the ID of the plane, in which the spine is located."""
        pattern = 'plane-(\d+)-spine-(\d+).*'
        r = re.compile(pattern)
        m = r.match(spine)
        return int(m.group(1))

    def __get_installed_flows(self, node, table_id = 0):
        """Returns flows on the given Bridge belonging to this Dcm.
        
        Args:
            node: Name of the Bridge.
            table_id: OpenFlow table ID.

        Returns:
            A list of flows.
        """
        b = self.__topo.get_bridge(node)
        flows = b.flow_get_conf_all(table_id)
        dcm_flows = []
        for f in flows:
            if f['id'].startswith('dcm_' + str(self.__id) + '_'):
                dcm_flows.append(f['id'])
        return dcm_flows

    def __install_subpath(self, at_node, to_node, usedaqring = False):
        """Installs a DCM flow at a Bridge.

        Args:
            at_node: Name of the Bridge, at which to install the flow.
            to_node: Name of the Bridge, to which the flow should be switched.
            usedaqring: True, if daqring port should be used for buffering.

        Returns:
            None.
        """

        #todo Change the hard-coded value specifying the first daqring port number
        daqring_id = self.__id + 100

        flow = Bridge.flow_init()
        flow['instructions']['instruction']['apply-actions']['action'].append({'order':0,'output-action':{}})
        flow['priority'] = 110
        flow['match']['ethernet-match'] = {'ethernet-type':{'type':2048}}
        flow['match']['ip-match'] = {'ip-protocol':6}
        flow['match']['ipv4-destination'] = self.__dip + '/32'

        b = self.__topo.get_bridge(at_node)
        dst_b = self.__topo.get_bridge(to_node)
        # If multiple links exist, use modulo
        of_dports = b.get_port_for_dst(dst_b.of_id())
        of_dport = of_dports[self.__id % (len(of_dports))]

        if usedaqring:
            # Flow from the daqring port to the output.
            flow_daqring_out = Bridge.flow_init()
            flow_daqring_out['id'] = 'dcm_' + str(self.__id) + '_outdaqringflow'
            flow_daqring_out['instructions']['instruction']['apply-actions']['action'].append(\
                    {'order':0,'output-action':{'output-node-connector':of_dport}})
            flow_daqring_out['match']['in-port'] = \
                    'openflow:'+str(b.of_id())+':'+str(daqring_id)
            flow_daqring_out['priority'] = 111
            b.flow_add(flow_daqring_out)

            # Flow from the intput to the daqring port (common basis for all incoming flows)
            flow['instructions']['instruction']['apply-actions']['action'][0]['output-action'] = \
                        {'output-node-connector':daqring_id}
        else:
            flow['instructions']['instruction']['apply-actions']['action'][0]['output-action'] = \
                        {'output-node-connector':of_dport}

        # Fill flow-specific data for all DCM flows and install them
        i = 0
        for inflow in self.__inflows:
            i += 1
            flow['match']['ipv4-source'] = inflow[1] + '/32'
            flow['match']['tcp-source-port'] = inflow[2]
            flow['match']['tcp-destination-port'] = inflow[0]
            flow['id'] = 'dcm_' + str(self.__id) + '_inflow_' + str(i)
            b.flow_add(flow)

    def __check_path(self, at_node, to_node):
        """Checks if the link between Bridges is UP.
        
        Args:
            at_node: Name of the first Bridge.
            to_node: Name of the second Bridge.

        Returns:
            True, if the path is UP.
        """
        at_b = self.__topo.get_bridge(at_node)
        to_b = self.__topo.get_bridge(to_node)
        #todo Single link assumed here
        of_port_atnode = at_b.get_port_for_dst(to_b.of_id())[0]
        of_port_tonode = to_b.get_port_for_dst(at_b.of_id())[0]

        if of_port_atnode == None:
            self.__o.fail('No port at {0:s} to destination {1:s}'.format(at_node, to_node))  
            return False

        if of_port_tonode == None:
            self.__o.fail('No port at {0:s} to destination {1:s}'.format(to_node, at_node))  
            return False

        available = True

        if at_b.get_port_status(of_port_atnode):
            pass
            #self.__o.okgreen('{0:s}:{1:02d} to {2:s} UP'.format(at_node, of_port_atnode, to_node))
        else:
            #self.__o.fail('{0:s}:{1:02d} to {2:s} DOWN'.format(at_node, of_port_atnode, to_node))  
            available = False
        if to_b.get_port_status(of_port_tonode):
            pass
            #self.__o.okgreen('{0:s}:{1:02d} to {2:s} UP'.format(to_node, of_port_tonode, at_node))  
        else:
            #self.__o.fail('{0:s}:{1:02d} to {2:s} DOWN'.format(to_node, of_port_tonode, at_node)) 
            available = False

        return available

    def spine_in_use(self):
        """Get the name of the spine Bridge in use by this Dcm.

        Args:

        Returns:
            The name of the spine Bridge or None.
        """
        spines = self.__topo.get_spines()
        for spine in spines:
            if len(self.__get_installed_flows(spine)):
                return spine
        return None

    def is_path_up(self):
        """Checks if the path in use is up.

        Args:

        Returns:
            True, if the currently used path is up.
        """
        spines = self.__topo.get_spines()
        for spine in spines:
            if len(self.__get_installed_flows(spine)):
                return self.is_path_available(spine, silent = True)
        return False

    def is_path_available(self, spine, silent = False):
        """Checks, if path through a given spine Bridge is available.

        Args:
            spine: The name of the spine Bridge to use.
            silent: True to supress the output.

        Returns:
            True, if the path through the given spine is up.
        """
        if not silent:
            self.__o.okblue('Checking path availability for DCM at {0} through spine {1}'.format(self.__dip, spine))

        available = True

        # Get the name of the pod Bridge to use
        dcm_pod = self.__get_pod_id()
        iplane = self.__get_plane_id(spine)
        dcm_pod_node = 'plane-' + str(iplane) + '-pod-' + str(dcm_pod)

        # Check from the pod-node to the end-node
        at = dcm_pod_node
        to = self.__node
        available &= self.__check_path(at, to)
        
        # Check from the spine-node to the pod-node
        at = spine
        to = dcm_pod_node
        available &= self.__check_path(at, to)

        # Check from the spine-node to all pod-nodes
        for i in range (self.__topo.get_npods()):
            node = 'plane-' + str(iplane) + '-pod-' + str(i + 1)
            if (i + 1) != dcm_pod:
                at = node
                to = spine
                available &= self.__check_path(at, to)
            # Check from the pod-node to all end-nodes
            for endnode in self.__topo.get_pod_endnodes(i+1):
                at = endnode
                to = node
                available &= self.__check_path(at, to)

        if not silent:
            if available:
                self.__o.okgreen('Path available.')
            else:
                self.__o.fail('Path not available.')

        return available    

    def remove_path(self):
        """Removes all flows from all Bridges belonging to this Dcm."""
        #self.__o.header('Removing DCM flows for destination {0}...'.format(self.__dip)) 
        for node in self.__topo.get_bridges():
            b = self.__topo.get_bridge(node)
            flow_ids = self.__get_installed_flows(node, table_id = 0)
            # first in-flow
            for flow_id in flow_ids:
                if 'inflow' in flow_id:
                    b.flow_delete('/table/0/flow/' + flow_id)
            # now out-flows
            for flow_id in flow_ids:
                if 'outdaqringflow' in flow_id:
                    b.flow_delete('/table/0/flow/' + flow_id)
        #self.__o.normal('Done.')

    def install_path(self, spine, usedaqring = False, at_dcm_pod_only = False):
        """Install flows to build a path for this Dcm's TCP flows.

        Args:
            spine: Spine switch to use. Can be None, if single pod in use.
            usedaring: True, if buffering in daqrings should be enabled.
            at_dcm_pod_only: True, if the daqrings should be used only
                             in the last pod switch before the Dcm.
        
        Returns:
            None
        """
        self.__o.header('Installing DCM flows for destination {0}...'.format(self.__dip)) 

        # Get the name of the pod Bridge to use
        dcm_pod = self.__get_pod_id()
        if spine:
            iplane = self.__get_plane_id(spine)
        else:
            iplane = 1
        dcm_pod_node = 'plane-' + str(iplane) + '-pod-' + str(dcm_pod)
        if not dcm_pod_node in self.__topo.get_bridges():
            # Probably back-back test setup
            dcm_pod_node = None

        self.__o.normal('Using {0}'.format(spine))
        
        if not usedaqring:
            at_dcm_pod_only = True

        # pod-node to the end-node
        if dcm_pod_node:
            self.__install_subpath(dcm_pod_node, self.__node, usedaqring = usedaqring)
        # the spine-node to the pod-node
        if at_dcm_pod_only and spine:
            self.__install_subpath(spine, dcm_pod_node, usedaqring = False)
        elif spine:
            self.__install_subpath(spine, dcm_pod_node, usedaqring = True)
        # all other pod-nodes to spine-nodes
        for i in range (self.__topo.get_npods()):
            node = 'plane-' + str(iplane) + '-pod-' + str(i + 1)
            if (i + 1) != dcm_pod:
                if at_dcm_pod_only:
                    self.__install_subpath(node, spine, usedaqring = False)
                else:
                    self.__install_subpath(node, spine, usedaqring = True)
            # end-nodes to pod switches 
            #todo Will install the flow at every endnode in the pod.
            #todo Better to use only the sources specified by the Dcms inflows.
            for endnode in self.__topo.get_pod_endnodes(i+1):
                if endnode != self.__node:
                    self.__install_subpath(endnode, node, usedaqring = False)

        # special case for the back-back setup
        if not dcm_pod_node:
            for endnode in self.__topo.get_pod_endnodes(dcm_pod):
                if endnode != self.__node:
                    self.__install_subpath(endnode, self.__node, usedaqring = False)

        self.__o.normal('Done.')

    def show_info(self):
        """Prints information about this Dcm."""
        self.__o.okblue('{0:<15s} {1:<20s} {2:6}'.format(self.__node, self.__dip, self.__pid))
        self.__o.okgreen('\t{0:s}'.format(', '.join((inflow[1]+':'+str(inflow[2])) for inflow in self.__inflows)))
        
class Bridge(object):
    """Represents an OpenFlow bridge."""

    def __init__(self, rest_client, of_id, ovsdb, max_flow_tables=1):
        """Creates a new Bridge object.

        Args:
            rest_client: RestClient object to access OpenDaylight's northbound REST interface.
            of_id: OpenFlow ID of the Bridge.
            ovsdb: Ovsdb object representing connection to the OVSDB server
                    that is to be used to configure the bridge.
            max_flow_tables: Maximum number of flow tables to be used.

        Returns:
            An instance of the Bridge class.
        """
        self.__r = rest_client

        self.__id = of_id
        self.__path = \
            'network-topology:network-topology/topology/flow:1/node/' + str(self.__id)
        self.__of_path = 'opendaylight-inventory:nodes/node/openflow:' + str(self.__id)
        self.__ovsdb = ovsdb
        self.__ntables = max_flow_tables

        self.__ports = {}

        # Check links to other bridges
        try:
            links = \
                self.__r.oper_ds_get('network-topology:network-topology/topology/flow:1/')['topology'][0]['link']
        except (rest.RestError, KeyError):
            links = []
        for l in links:
            if str(self.__id) in l['link-id'] and not '/' in l['link-id']:
                sport = int(l['link-id'].rsplit(':',1)[1])
                dst = l['destination']['dest-tp']
                dst = dst.rsplit(':',2)
                self.__ports[sport] = (int(dst[1]), int(dst[2]))

    @staticmethod
    def flow_init():
        """Returns a dictionary to be used as basis for flow creation."""
        flow = {'flow-name':'',
                'table_id':0,
                'priority':1,
                'id':'default_in',
                'hard-timeout':0,
                'idle-timeout':0,
                'match':{},
                'instructions':{'instruction':
                    {'order':0,
                    'apply-actions':
                        {'action':[]}
                    }}
               }
        return flow

    def flow_get_conf_all(self, table_id = 0):
        """Returns all flows from the configuration datastore."""
        try:
            flows = self.__r.conf_ds_get(self.__of_path + \
                    '/table/' + str(table_id))
            flows = flows['flow-node-inventory:table'][0]['flow']
        except (rest.RestError, KeyError):
            flows = []
        return flows

    def flow_add(self, flow):
        """Inserts flow into the configuration datastore."""
        f = {'flow':[flow]}
        self.__r.conf_ds_put(self.__of_path + \
                '/table/' + str(flow['table_id']) + \
                '/flow/' + flow['id'], f)

    def flow_delete(self, path):
        """Removes path from the configuration datastore."""
        try:
            self.__r.conf_ds_delete(self.__of_path + path)
        except rest.RestError:
            pass

    def flow_delete_all(self):
        """Removes all flows."""
        for t in range(self.__ntables):
            self.flow_delete('/table/' + str(t))

    def name(self):
        """Returns the name of the Ovsdb object associated wit this Bridge."""
        return self.__ovsdb.name()

    def of_id(self):
        """Returns OpenFlow ID of this Bridge."""
        return self.__id

    def mgmt_ip(self):
        """Returns the IP of the OVSDB server."""
        return self.__ovsdb.ip()

    def get_ports(self):
        """Return the list of the ports on this Bridge."""
        return self.__ports

    def get_port_for_dst(self, dst_of_id):
        """Returns ports on this Bridge that are connected to another Bridge.

        Args:
            dst_of_id: OpenFlow id of the remote bridge.

        Returns:
            A list of ports on this bridge.
        """
        ports = []
        for p in self.__ports:
            if self.__ports[p][0] == dst_of_id:
                ports.append(p)
        return ports

    def get_mac(self):
        """Returns the MAC address of this Bridge."""
        return ':'.join('%02X' % ((self.__id >> 8*i) & 0xff) for i in reversed(xrange(6)))

    def get_port_status(self, port):
        """Returns True if port is up, False otherwise."""
        status = True
        try:
            port = self.__r.oper_ds_get(self.__of_path + \
                '/node-connector/openflow:' + str(self.__id) + ':' + str(port))['node-connector'][0]
            if port['flow-node-inventory:configuration'] == 'PORT-DOWN':
                status = False
            if port['flow-node-inventory:state']['link-down']:
                status = False
        except rest.RestError:
            status = False
        return status

    def get_port_stats(self, port):
        """Return port statistics."""
        stats = {'packets':{'received':0,'transmitted':0},
                 'bytes':{'received':0,'transmitted':0},
                 'receive-drops':0,
                 'transmit-drops':0,
                 'receive-errors':0,
                 'transmit-errors':0,
                 'receive-frame-error':0,
                 'receive-over-run-error':0,
                 'receive-crc-error':0,
                 'collision-count':0}
        try:
            stats = self.__r.oper_ds_get(self.__of_path + \
                '/node-connector/openflow:' + str(self.__id) + ':' + str(port) + \
                '/opendaylight-port-statistics:flow-capable-node-connector-statistics')
            stats = stats['opendaylight-port-statistics:flow-capable-node-connector-statistics']
        except rest.RestError:
            pass
        return stats
            

class Topology(object):
    """Class to represent the entire network topology. Consists of Bridge objects.
    
    Specific node naming convetion and IP addressing is assumed:
        plane-X-pod-Y: Leaf switch in the pod Y in the plane X.
        plane-X-spine-Y: Spine switch Y in the plane X.
        CUSTOMNAME-podY: End node in pod Y.
    IP address are hard-coded at this point. The scheme:
        20.0.Y.Z: IP address of a node in pod Y. Z is assumed to math the fourth byte
                  of the OVSDB server assosiacted with this end-node.

    See examples for more information.
    """

    def __init__(self, name, rest_client, silent=False):
        """Creates a new Topology object based on the current state of the datastore.

        Args:
            name: Name of the topology.
            rest_client: RestClient object to access OpenDaylight's northbound REST interface.
            silent: Supresses output, if False.

        Returns:
            An instance of the Topology class.
        """
        self.__o = output.Output(silent)

        self.__r = rest_client
        self.__id = name

        self.__bridges = {}

        self.__update()

    @staticmethod
    def ip_to_pod(ip):
        """Returns the pod number for the given IP."""
        return 'pod' + ip.split('.')[2]

    def get_name_from_of_id(self, of_id):
        """Returns the name of the Bridge with given OpenFlow ID."""
        for n,b in self.__bridges.iteritems():
            if b.of_id() == of_id:
                return n
        return None

    def __update(self):
        """Refreshes the list of Bridges in the topology."""
        ovsdbs = ovsdb.OvsdbClient.get_all_nodes(self.__r)
        for o in ovsdbs:
            if o.is_bridge():
                self.__bridges[o.name()] = Bridge(self.__r, o.of_id(), o)

    def get_bridges(self):
        """Returns the list of Bridges in the topology."""
        return self.__bridges

    def get_bridge(self, name):
        """Returns the Bridge object with the given name."""
        return self.__bridges[name]

    def get_spines(self):
        """Returns a list of Bridges that are spines."""
        spines = []
        for b in self.__bridges:
            if 'spine' in b:
                spines.append(b)
        return spines

    def get_endnodes(self):
        """Returns a list of Bridges that are end-nodes."""
        endnodes = []
        for b in self.__bridges:
            if (not 'plane' in b) and ('pod-' in b):
                endnodes.append(b)
        return endnodes

    def get_pod_endnodes(self, i):
        """Returns a list of Bridges in the given pod number."""
        pod_endnodes = []
        for b in self.__bridges:
            if (not 'plane' in b) and ('pod-'+str(i) in b):
                pod_endnodes.append(b)
        return pod_endnodes

    def get_npods(self):
        """Returns the total number of pods."""
        npods = 0
        for b in self.__bridges:
            if 'plane-1-pod-' in b:
                npods += 1
        return npods

    def show_bandwidth(self, delay=60):
        """Shows bandwidth utilization for all links in this Topology."""
        #self.__update()
        topo = {}

        for n,b in self.__bridges.iteritems():
            topo[n] = {}
            for p,l in b.get_ports().iteritems(): 
                topo[n][p] = {}
                topo[n][p]['dest'] = self.get_name_from_of_id(l[0])
                stats = b.get_port_stats(p) 
                topo[n][p]['tstamp'] = time.time()
                topo[n][p]['rx'] = stats['bytes']['received']
                topo[n][p]['tx'] = stats['bytes']['transmitted']
                topo[n][p]['drops'] = 0
                for k,v in stats.iteritems():
                    if 'drops' in k or 'errors' in k:
                        topo[n][p]['drops'] += v

        time.sleep(delay)
        self.__o.header(time.ctime())

        spines = [n for n in topo if '-spine-' in n]
        leafs = [n for n in topo if '-pod-' in n]
        endnodes = [n for n in topo if n.startswith('pod-')]
        nodes = spines + leafs + endnodes
        for n in nodes:
            for p,s in topo[n].iteritems(): 
                stats = self.__bridges[n].get_port_stats(p) 
                tdiff = (time.time() - s['tstamp']) * 1.0e9
                rx = (stats['bytes']['received'] - s['rx']) * 8.0 / tdiff
                tx = (stats['bytes']['transmitted'] - s['tx']) * 8.0 / tdiff
                for k,v in stats.iteritems():
                    if 'drops' in k or 'errors' in k:
                        s['drops'] -= v
                self.__o.normal('{0:<20s} -> {1:<20s}    Rx {2:6.3f} Tx {3:6.3f}    Drops {4:5d}'.format(\
                        n, s['dest'], rx, tx, -s['drops']))
                
    def show(self):
        """Shows the topology."""
        #self.__update()
        for n,b in self.__bridges.iteritems():
            self.__o.header('{0} of_id:{1:d}'.format(n, b.of_id()))
            for p,l in b.get_ports().iteritems(): 
                if b.get_port_status(p):
                    self.__o.okgreen('\t{0:02d} <-> {1:>20s}:{2:02d}'.format(p, self.get_name_from_of_id(l[0]), l[1]))  
                else:
                    self.__o.fail('\t{0:02d} <-> {1:>20s}:{2:02d}'.format(p, self.get_name_from_of_id(l[0]), l[1]))  

    def check_links(self, silent = False):
        """Shows status of the links in this Topology. Returns True if all are up."""
        #self.__update()
        all_up = True
        for n,b in self.__bridges.iteritems():
            for p,l in b.get_ports().iteritems(): 
                if not b.get_port_status(p):
                    all_up = False
                    if not silent:
                        self.__o.header('{0} of_id:{1:d}'.format(n, b.of_id()))
                        self.__o.fail('\t{0:02d} (DOWN) <-> {1:>20s}:{2:02d}'.format(p, self.get_name_from_of_id(l[0]), l[1]))  
        if all_up:
            if not silent:
                self.__o.okgreen('All links UP')
            return True
        return False


    def clear_flows(self):
        """Removes all flows in all Bridges in this Topology."""
        self.__o.header('Removing all flows...')
        #self.__update()
        for n in self.__bridges:
            self.__bridges[n].flow_delete_all()
        self.__o.normal('Done.')

    def set_default_flows(self, default_port=2):
        """Installs default flows on all Bridges for an IP-only network.

        Default flows use only a single path through the network.
        Flows to change the DMAC address on the end-nodes are also installed. 

        Args:
            default_port: default port to use, if multiple paths exist on a Bridge.
        """
        #self.__update()
        # End-hosts
        self.__o.header('Installing default flows for OVSs running on end-hosts...')
        flow = Bridge.flow_init()
        flow['instructions']['instruction']['apply-actions']['action'].append({'order':0,'output-action':{}})
        flow['priority'] = 98
        for n in self.get_endnodes():
            # Outgoing flows
            flow['flow-name'] = 'default_out'
            flow['id'] = 'default_out'
            flow['match']['in-port'] = 'openflow:'+str(self.__bridges[n].of_id())+':LOCAL'
            flow['instructions']['instruction']['apply-actions']['action'][0]['output-action'] = \
                    {'output-node-connector':default_port}
            self.__bridges[n].flow_add(flow)

            # Incoming flows. Update DMAC to the local one.
            # Otherwise Linux will reject incoming TCP connections.
            dmac = self.__bridges[n].get_mac()
            flow['instructions']['instruction']['apply-actions']['action'][0]['output-action'] = \
                    {'output-node-connector':'LOCAL'}
            flow['instructions']['instruction']['apply-actions']['action'][0]['order'] = 1
            flow['instructions']['instruction']['apply-actions']['action'].append(\
                    {'order':0,'set-dl-dst-action':{'address':dmac}})
            # Flows coming on port 1.
            flow['flow-name'] = 'default_in1'
            flow['id'] = 'default_in1'
            flow['match']['in-port'] = 'openflow:'+str(self.__bridges[n].of_id())+':1'
            self.__bridges[n].flow_add(flow)
            # Flows coming on port 2.
            flow['flow-name'] = 'default_in2'
            flow['id'] = 'default_in2'
            flow['match']['in-port'] = 'openflow:'+str(self.__bridges[n].of_id())+':2'
            self.__bridges[n].flow_add(flow)
            #todo Make more generic to include more than two uplinks.
            # Actions will be different for the next nodes.
            del(flow['instructions']['instruction']['apply-actions']['action'][1])
        self.__o.normal('Done.')

        # Spine switches
        self.__o.header('Installing default flows at spines...')
        flow = Bridge.flow_init()
        flow['instructions']['instruction']['apply-actions']['action'].append({'order':0,'output-action':{}})
        flow['priority'] = 98
        flow['match']['ethernet-match'] = {'ethernet-type':{'type':2048}}
        for n in self.__bridges:
            if ('spine' in n):
                spine = self.__bridges[n]
                self.__o.okblue(n)
                for dst_port,dst in spine.get_ports().iteritems(): 
                    dst_pod_name = self.get_name_from_of_id(dst[0])
                    #todo IP forwarding hard-coded in IP-addresses and spine, leaf numbers in their names
                    dst_ip = '20.1.' + dst_pod_name.split('-')[3] + '.0/24'
                    flow['flow-name'] = 'to-' + dst_pod_name
                    flow['id'] = flow['flow-name']
                    flow['match']['ipv4-destination'] = dst_ip 
                    flow['instructions']['instruction']['apply-actions']['action'][0]['output-action'] = \
                            {'output-node-connector':dst_port}
                    self.__o.normal('Destination pod {0}: {1}'.format(dst_pod_name, dst_ip))
                    spine.flow_add(flow)
        self.__o.normal('Done.')

        # Leaf switches
        self.__o.header('Installing default flows at pods...')
        flow = Bridge.flow_init()
        flow['instructions']['instruction']['apply-actions']['action'].append({'order':0,'output-action':{}})
        flow['priority'] = 97
        flow['match']['ethernet-match'] = {'ethernet-type':{'type':2048}}
        for n in self.__bridges:
            if ('pod' in n and 'plane' in n):
                pod = self.__bridges[n]
                self.__o.okblue(n)
                pod_no = n.split('-')[3]
                for dst_port,dst in pod.get_ports().iteritems(): 
                    dst_name = self.get_name_from_of_id(dst[0])
                    if 'spine' in dst_name:
                        if default_port != int(dst_name.split('-')[3]):
                            continue
                        dst_ip = '20.1.0.0/16'
                    else:
                        dst_ip = '20.1.' \
                                + pod_no + '.' \
                                + self.__bridges[dst_name].mgmt_ip().split('.')[3] \
                                + '/32' 
                    flow['match']['ipv4-destination'] = dst_ip 
                    flow['flow-name'] = 'to-' + dst_name
                    flow['id'] = flow['flow-name']
                    flow['instructions']['instruction']['apply-actions']['action'][0]['output-action'] = \
                            {'output-node-connector':dst_port}
                    self.__o.normal('Destination {0}: {1}'.format(dst_name, dst_ip))
                    pod.flow_add(flow)
        self.__o.normal('Done.')

    def set_drop_flows(self):
        """Installs a rule on all Bridges to drop all packets not matching any other rules."""
        #self.__update()
        self.__o.header('Installing drop flows...')
        flow = Bridge.flow_init()
        flow['priority'] = 1
        flow['flow-name'] = 'drop'
        flow['id'] = 'drop'
        flow['instructions']['instruction']['apply-actions']['action'].append({'order':0, 'drop-action':{}})
        for b in self.__bridges.values():
            b.flow_add(flow)
        self.__o.normal('Done.')

    def set_lldp_flows(self):
        """Installs a rule on all Bridges to forward all LLDP packets to the conteoller."""
        self.__o.header('Installing LLDP flows...')
        flow = Bridge.flow_init()
        flow['priority'] = 200
        flow['flow-name'] = 'lldp'
        flow['id'] = 'lldp'
        flow['match']['ethernet-match'] = {'ethernet-type':{'type':0x88cc}}
        flow['instructions']['instruction']['apply-actions']['action'].append(
                {'order':0,'output-action':{'output-node-connector':'CONTROLLER','max-length':65535}})
        for b in self.__bridges.values():
            b.flow_add(flow)
        self.__o.normal('Done.')

    def set_dhcp_paths(self, node_name='plane-1-spine-1', of_port=1):
        """Installs rules on all Bridges to forward DHCP packets.

        Args:
            node_name: the name of the Bridge, to which the DHCP server is connected to.
            of_port: OpenFlow port number of the port on this Bridge connected to the DHCP server.
        """
        self.__o.header('Installing DHCP flows...')
        #self.__update()

        #todo at the moment DHCP server is assumed to be connected to one of the spine switches
        b = self.__bridges[node_name]
        flow = Bridge.flow_init()
        flow['flow-name'] = 'dhcp'
        flow['id'] = 'dhcp'
        flow['priority'] = 100
        flow['match']['ethernet-match'] = {'ethernet-type':{'type':2048}}
        flow['match']['ip-match'] = {'ip-protocol':17}
        flow['match']['udp-source-port'] = 68
        flow['match']['udp-destination-port'] = 67
        # Install flow to the destination DHCP server.
        self.__o.normal('Adding destination flow at {0}...'.format(node_name))
        flow['instructions']['instruction']['apply-actions']['action'].append({'order':0, 'output-action': \
                {'output-node-connector':of_port}})
        b.flow_add(flow)
        self.__o.normal('Done.')
        # Install flow at the pod switches.
        for l in b.get_ports().values(): 
            pod_name = self.get_name_from_of_id(l[0])
            pod_port = l[1] 
            b_pod = self.__bridges[pod_name]
            flow['instructions']['instruction']['apply-actions']['action'][0]['output-action'] = \
                    {'output-node-connector':pod_port}
            self.__o.normal('Adding flow to {0} at {1}...'.format(node_name, pod_name))
            b_pod.flow_add(flow)
            self.__o.normal('Done.')
            # Install flow at the host switches.
            for l_pod in b_pod.get_ports().values(): 
                host_name = self.get_name_from_of_id(l_pod[0])
                host_port = l_pod[1] 
                if 'plane' in host_name:
                    continue
                b_host = self.__bridges[host_name]
                flow['instructions']['instruction']['apply-actions']['action'][0]['output-action'] = \
                        {'output-node-connector':host_port}
                self.__o.normal('Adding flow to {0} at {1}...'.format(pod_name, host_name))
                b_host.flow_add(flow)
                self.__o.normal('Done.')
        self.__o.normal('DHCP flows installed.')
