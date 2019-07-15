#!/bin/sh

# Build RocksDB static library
cd deps/rocksdb
make -sj64 static_lib
cd ../../

# Build and run Shinjuku.
make clean
make -sj64
LD_PRELOAD=./deps/opnew/dest/libnew.so ./dp/shinjuku
