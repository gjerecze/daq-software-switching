daqswitch project
=================

An DPDK application implementing a simple software switch for data acquisition. This is a proof-of-concept project, so the functionality is limited.

For detailed description see the following research paper:

"A Lossless Switch for Data Acquisition" - https://cds.cern.ch/record/2038585

Installation
------------
Compile DPDK 1.8.0 according to the instructions in this project.

Compile the application:
```
cd daqswitch
flags='-DDP_TX_DRAIN_INTERVAL=0 -DDP_RX_POLL_INTERVAL=0' # define poll/drain intervals in the datapath
export RTE_SDK="/afs/cern.ch/work/g/gjerecze/tmp/dpdk-1.8.0" # path DPDK SDK
export RTE_TARGET=x86_64-native-linuxapp-gcc-release # DPDK RTE_TARGET
EXTRA_CFLAGS=$flags make O=#OUTPUT_PATH 
```

Usage
-----
Arguments before "--" are specific to DPDK. Arguments after are specific to this application.
```
daqswitch -c 0xffff -n4 -- --disable-cli # with command line interface
daqswitch -c 0xffff -n4 # without command line interface
```

Changing datapaths
------------------
The way the packets are processed by the application can be influenced by changing the datapath implementation. 
This can be done be setting the correct name in the daqswitch.config, which should match one of the datapaths implemented
in the `dp` directory:

1. oq_hwq: Output-queueing with hardware queues. There is a single hw rx queue for each port. 
Received packets are put directly into one of the hw tx queue after consulting the forwarding table.
2. voq_hwq: Virtual-output-queueing with hardware queues. There is as many hw rx queues for each port as many ports on the switch 
in total. Packets are filtered to appropriate queues using hardware 5tuple filters. Recevied packets are then put directly into 
the hw tx queue without consulting the forwarding table.
3. voq_swq: Virtual-output-queueing with software queues. Uses DPDK pipelines. There is a default pipeline interconnecting all 
physical ports and performing lookups in the forwarding table. There is additional logic to detect specific flows (data acquisition
flows based on the ATLAS exeriment at CERN). For these flows hardware filters are created again to queue the packets in specific hw rx queues.
Next, the packets are queued in dedicated sw rings before being put into the hw tx queues.
4. skeleton: New implementations can be build using this skeleton.

Setting flows
-------------
There is no logic to learn MAC addresses implemented. Flows must be added manually. 
For now the only option is to hard-code them into the application. This is done in 
the `dp_install_default_tables` in the datapath implementation.
