#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The FM-1's second core changes nothing in the sound: a script rendered on one core (HOST_CPU2=0) and on two (the
# host runs core 1's jobs where core 0 waits for them) gives the same file, bit for bit.
#   tests/cpu2_same.sh script...
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER='"dev"' -Ifirmware/hal -Ifirmware/src -Ifirmware/gen -o build/host/cpu2 firmware/src/dinghy.c
for s in "$@"; do
    HOST_CPU2=0 build/host/cpu2 build/host/cpu2 "$s" > /dev/null && cp build/host/cpu2.wav build/host/cpu2-one.wav
    HOST_CPU2=1 build/host/cpu2 build/host/cpu2 "$s" > /dev/null
    if cmp -s build/host/cpu2-one.wav build/host/cpu2.wav; then echo "$s: one core = two cores"; else echo "$s: the sound differs"; exit 1; fi
done
