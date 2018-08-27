#!/bin/sh

make clean
make -sj64
LD_PRELOAD=/home/kkaffes/shinjuku/deps/opnew/dest/libnew.so ./dp/ix
