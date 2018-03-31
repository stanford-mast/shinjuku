#!/bin/sh

sudo sh -c 'for i in /sys/devices/system/node/node*/hugepages/hugepages-2048kB/nr_hugepages; do echo 4096 > $i; done'
./deps/dpdk/tools/dpdk_nic_bind.py -u 0000:01:00.0
./deps/dpdk/tools/dpdk_nic_bind.py -u 0000:01:00.1
sudo insmod deps/dune/kern/dune.ko
sudo insmod deps/pcidma/pcidma.ko
