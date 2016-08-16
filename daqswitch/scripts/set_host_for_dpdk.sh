#!/bin/sh

# Set CPU affinity for the kernel's write-back bdi-flush threads to the housekeeping core
echo 1 > /sys/bus/workqueue/devices/writeback/cpumask &> /dev/null

# Delay the annoying vmstat timer far away
sysctl vm.stat_interval=360 &> /dev/null

# Shutdown nmi watchdog as it uses perf events
sysctl -w kernel.watchdog=0 &> /dev/null

# Set the highest frequency
modprobe acpi-cpufreq &> /dev/null
cpupower frequency-set --governor userspace  &> /dev/null
cpupower --cpu all frequency-set --freq 2.70GHz &> /dev/null

# Remove ixgbe
rmmod ixgbe &> /dev/null
