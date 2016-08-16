#!/bin/bash

release=${1:-1.8.0}
target=${2:-release}
target="x86_64-native-linuxapp-gcc-$target"

export RTE_SDK="/nfs/dpdk/dpdk-$release"
export RTE_TARGET=$target
