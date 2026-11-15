#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the emulator's firmware: firmware/src/dinghy.c as WebAssembly, on web/emu/host_web.c (a fake FM-1) in place
# of tests/host.c, in Emscripten's Docker image. Out: build/emu/drumtonic-emu.wasm (the page runs it: web/emu/emu-worklet.js).
#   web/emu/build.sh [VERSION]       VERSION: APP_VER, what the screen says (default 0.0, as ./build.sh)
#   EMU_DOCKER=0 web/emu/build.sh    with the emcc and wasm-opt on PATH (an emsdk of the same version) instead
set -e
cd "$(dirname "$0")/../.."
IMAGE=emscripten/emsdk:4.0.10                   # pinned: the same compiler gives the same .wasm (amd64 only)
VER="${1:-0.0}"
mkdir -p build/emu
run() {
    if [ "${EMU_DOCKER:-1}" = 1 ]; then
        docker info >/dev/null 2>&1 || { echo "web/emu/build.sh: Docker is not running"; exit 1; }
        docker run --rm --platform linux/amd64 -v "$PWD:/src" -w /src "$IMAGE" sh -c "$1"
    else
        sh -c "$1"
    fi
}
# platform.c includes ../../tests/host.c from its own folder: a copy of firmware/ beside a tests/ that holds this file
# (the tree itself stays as it is). Then Asyncify (Binaryen) lets the page suspend the app in emu_yield and resume it.
run "set -e
T=build/emu/tree; rm -rf \$T; mkdir -p \$T/tests; cp -R firmware \$T/; cp web/emu/host_web.c \$T/tests/host.c
emcc -O2 -Wall -Wno-unused-function -DDINGHY_HOST -DAPP_VER='\"$VER\"' -I\$T/firmware/hal -I\$T/firmware/src \
     -I\$T/firmware/gen \$T/firmware/src/dinghy.c -o build/emu/app.wasm \
     --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1MB -sINITIAL_MEMORY=16MB -sALLOW_MEMORY_GROWTH=0
W=wasm-opt; [ -x /emsdk/upstream/bin/wasm-opt ] && W=/emsdk/upstream/bin/wasm-opt
\$W build/emu/app.wasm --asyncify --pass-arg=asyncify-imports@env.emu_yield --pass-arg=asyncify-ignore-indirect \
     --enable-bulk-memory --enable-bulk-memory-opt --enable-nontrapping-float-to-int --enable-sign-ext \
     --enable-mutable-globals --enable-multivalue --enable-reference-types -O2 -o build/emu/drumtonic-emu.wasm
rm -rf \$T build/emu/app.wasm"
ls -l build/emu/drumtonic-emu.wasm | awk '{print "emu    build/emu/drumtonic-emu.wasm", $5, "B"}'
