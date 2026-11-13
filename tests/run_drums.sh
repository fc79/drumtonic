#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The drum engine on this computer (tests/drum_render.c): a WAV per kit channel and a demo groove per kit in
# build/drums/, the checks, the render's cost.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/drums
cc -O2 -Wall -Wextra -Wno-unused-function -Ifirmware/src -Ifirmware/gen -o build/drums/drum_render tests/drum_render.c -lm
build/drums/drum_render
