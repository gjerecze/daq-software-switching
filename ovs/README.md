daqr patch for OVS
==================

Patch adding support for data acquisition rings (DPDK daqr) in Open vSwitch (OVS) 2.4.0. Current version is based on DPDK 2.2.0.

For detailed description see the following research paper:

"A Lossless Network for Data Acquisition" - https://cds.cern.ch/record/2162697

And also:

"Analogues between Tuning TCP for Data Acquisition and Datacenter Networks" - https://cds.cern.ch/record/2019830

"A Lossless Switch for Data Acquisition" - https://cds.cern.ch/record/2038585

Installation
------------
Apply patch on top of OVS 2.4.0. Compile DPDK 2.2.0 and OVS 2.4.0 according to the instructions in those projects.

OVS Configure options:

`./configure --with-dpdk=$DPDK_BUILD_PATH --enable-daq --enable-daq-fc`

1. --enable-daq: Adds support for daqr device
2. --enable-daq-fc: Enables flow control for DPDK devices (pause frames)


Preprocessor defines:

1. DPDK_DAQ_MAX_NB_MBUF: determines the maximum number of packets that can be buffered inside of OVS
2. DPDK_DAQ_NIC_PORT_RX_Q_SIZE and DPDK_DAQ_NIC_PORT_TX_Q_SIZE: determines the hardware tx/rx queue sizes
3. DPDK_DAQ_RING_SIZE: queue size (in number of packets) of a single daqr device

Example to compile OVS (assuming DPDK was built in advance):
```
wget https://github.com/gjerecze/daq-software-switching/blob/master/ovs/ovs_2.4.0_daqr.patch
wget http://openvswitch.org/releases/openvswitch-2.4.0.tar.gz
tar -xzf openvswitch-2.4.0.tar.gz
cd openvswitch-2.4.0/
patch -p0 < ../ovs_2.4.0_daqr.patch
./boot.sh
DPDK_DIR=/home/gjerecze/dpdk/dpdk-2.2.0/
DPDK_BUILD=$DPDK_DIR/x86_64-native-linuxapp-gcc-release
./configure --with-dpdk=$DPDK_BUILD --enable-daq
make CFLAGS='-O3 -march=native -g -DDPDK_DAQ_MAX_NB_MBUF=8388608 -DDPDK_DAQ_NIC_PORT_RX_Q_SIZE=4096 -DDPDK_DAQ_NIC_PORT_TX_Q_SIZE=4096 -DDPDK_DAQ_RING_SIZE=16384' -j8
sudo make install
```

Usage
------------
Use DPDK with ovs-vswitchd according to instructions in OVS.

To create a single daqring device with id 0 and openflow port number of 100:

`ovs-vsctl add-port br0 dpdkdaqr0  -- set Interface dpdkdaqr0 type=dpdkdaqr ofport:100`

To set rate limitation on the output of daqring it is required to set poll interval (in CPU ticks) and the burst (total number of packets polled from daqring during the poll interval):

```ovs-appctl netdev-dpdk-daqring/set-poll-tsc dpdkdaqr0 102400
ovs-appctl netdev-dpdk-daqring/set-max-burst dpdkdaqr0 32```

If poll==0, no rate limits are applied.

To redirecti packets to darings appropriate OpenFlow rules must be created. 

Example:

```
ovs-ofctl add-flow br0 priority=100,dl_type=0x800,nw_dst=20.1.11.1,actions=output:100
ovs-ofctl add-flow br0 priority=101,in_port=100,action=output:1
```

In this example every packet with destination IP of 20.1.11.1 will be redirected to daqring for buffering. Packets polled from this daqring will be later dequeued and output to port 1 (note flow priorities).


