#!/bin/sh
set -e
set -x
echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
make clean
echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo
echo 1700000 | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_max_freq
echo 1700000 | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_min_freq
make -sj64
echo "" > output.txt
./dp/shinjuku
