#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The FM-1 app on this computer under AddressSanitizer: every script given, and no read or write out of an array's
# bounds (on the FM-1 such a write lands on whatever is next in RAM: say() ran over into key_role until 2026-11-10).
#   tests/asan.sh script.txt ...
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
cc -O1 -g -fsanitize=address -fno-omit-frame-pointer -w -DDINGHY_HOST -DAPP_VER='"dev"' -Ifirmware/hal -Ifirmware/src \
   -Ifirmware/gen -o build/host/dinghy-asan firmware/src/dinghy.c
for s in "$@"; do
    if ! build/host/dinghy-asan build/host/dinghy-asan "$s" > build/host/asan.log 2>&1 || grep -q "ERROR: AddressSanitizer" build/host/asan.log; then
        echo "$s:"; grep -E "ERROR: AddressSanitizer|#[0-4] " build/host/asan.log | head -8; exit 1
    fi
    echo "$s: clean"
done
rm -f build/host/dinghy-asan-*.ppm build/host/dinghy-asan.wav
