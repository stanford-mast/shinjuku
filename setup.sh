#!/bin/sh

# Set huge pages
sudo sh -c 'for i in /sys/devices/system/node/node*/hugepages/hugepages-2048kB/nr_hugepages; do echo 8192 > $i; done'

# Unbind NICs
hostname=$(hostname)
if [ $hostname = 'goliath-2' ]; then
  ./deps/dpdk/tools/dpdk_nic_bind.py -u 81:00.0
  ./deps/dpdk/tools/dpdk_nic_bind.py -u 81:00.1
  ./deps/dpdk/tools/dpdk_nic_bind.py -u 01:00.0
  ./deps/dpdk/tools/dpdk_nic_bind.py -u 01:00.1
else
   echo 'Expression evaluated as false'
fi

# Insert kernel modules
sudo insmod deps/dune/kern/dune.ko
sudo insmod deps/pcidma/pcidma.ko
