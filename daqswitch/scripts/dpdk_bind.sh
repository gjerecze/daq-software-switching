#!/bin/bash

. /nfs/daqswitch/scripts/set_dpdk_env.sh $1 $2

modprobe uio
insmod $RTE_SDK/$RTE_TARGET/kmod/igb_uio.ko

#$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=igb_uio 04:00.1
#$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=igb_uio 04:00.2
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=igb_uio 85:00.1
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=igb_uio 85:00.0
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=igb_uio 83:00.0
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=igb_uio 83:00.1
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=igb_uio 0a:00.0
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=igb_uio 0a:00.1
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=igb_uio 08:00.0
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=igb_uio 08:00.1
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=igb_uio 81:00.0
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=igb_uio 81:00.1
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=igb_uio 87:00.0
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=igb_uio 87:00.1
