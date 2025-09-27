#!/usr/bin/env bash
set -euo pipefail
sudo insmod kernel/nxp_simtemp.ko
dmesg | tail -n 30
sudo rmmod nxp_simtemp
