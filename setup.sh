#!/bin/sh

# Remove kernel modules
rmmod pcidma
rmmod dune

# Set huge pages
sudo sh -c 'for i in /sys/devices/system/node/node*/hugepages/hugepages-2048kB/nr_hugepages; do echo 8192 > $i; done'

# Unbind NICs
 ./deps/dpdk/tools/dpdk_nic_bind.py --force -u 05:00.0

# Build required kernel modules.
make -sj64 -C deps/dune
make -sj64 -C deps/pcidma
make -sj64 -C deps/dpdk config T=x86_64-native-linuxapp-gcc
make -sj64 -C deps/dpdk
make -sj64 -C deps/rocksdb static_lib
make -sj64 -C deps/opnew

# Insert kernel modules
sudo insmod deps/dune/kern/dune.ko
sudo insmod deps/pcidma/pcidma.ko

# Create RocksDB database
make -C db
cd db
rm -r my_db
./create_db
cd ../
