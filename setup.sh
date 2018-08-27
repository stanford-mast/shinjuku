#!/bin/sh

# Remove kernel modules
rmmod pcidma
rmmod dune

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
  ./deps/dpdk/tools/dpdk_nic_bind.py -u 82:00.0
  ./deps/dpdk/tools/dpdk_nic_bind.py -u 82:00.1
  ./deps/dpdk/tools/dpdk_nic_bind.py -u 03:00.0
  ./deps/dpdk/tools/dpdk_nic_bind.py -u 03:00.1
fi

# Build required kernel modules.
make -sj64 -C deps/dune
make -sj64 -C deps/pcidma
make -sj64 -C deps/dpdk config T=x86_64-native-linuxapp-gcc
make -sj64 -C deps/dpdk

# Insert kernel modules
sudo insmod deps/dune/kern/dune.ko
sudo insmod deps/pcidma/pcidma.ko
