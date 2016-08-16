#!/bin/bash

. /nfs/daqswitch/scripts/set_dpdk_env.sh $1 $2

#$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=none 04:00.1
#$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=none 04:00.2
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=none 85:00.1
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=none 85:00.0
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=none 83:00.0
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=none 83:00.1
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=none 0a:00.0
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=none 0a:00.1
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=none 08:00.0
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=none 08:00.1
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=none 81:00.0
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=none 81:00.1
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=none 87:00.0
$RTE_SDK/tools/dpdk_nic_bind.py --force --bind=none 87:00.1

rmmod igb_uio
