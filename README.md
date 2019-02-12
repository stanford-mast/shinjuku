## Overview

Shinjuku is a dataplane operating system project from Stanford. It provides datacenter applications with:
* low-latency (including at the tail)
* high-throughput
* preemptive scale that can handle any service time distribution

This work was published on NSDI'19:

https://www.usenix.org/conference/nsdi19/presentation/kaffes

Shinjuku is licensed under an MIT-style license. See LICENSE.

## Requirements

Shinjuku requires Intel DPDK and an Intel 82599 NIC. Support for more NICs is on the way.

## Setup Instructions

There is currently no binary distribution of Shinjuku. You will therefore have to compile it from source. Additionally, you will need to fetch and compile the source dependencies:

1. fetch the dependencies:
   ```
   ./deps/fetch-deps.sh
   sudo apt-get install libconfig-dev libnuma-dev
   ```

2. Build the dependencies, set up the environment, and run Shinjuku:
   ```
   cp shinjuku.conf.sample shinjuku.conf
   # modify at least host_addr, gateway_addr, devices, cpu, and arp address (add client address)
   ./setup.sh
   ./build_and_run.sh
   ```

   Then, try from another another Linux host:
   ```
   # add arp entry for the Shinjuku server
   sudo arp -s <IP> <MAC_ADDRESS>
   ./client/latency_client <IP> <PORT> <RPS> <SPIN_TIME>>
   ```
