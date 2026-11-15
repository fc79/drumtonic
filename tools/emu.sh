#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The emulator with the firmware as it is now in this checkout: builds the .wasm (web/emu/build.sh, Docker) and the
# bare page (web/emu/dev.html), then opens it. One file, build/emu/drumtonic-emu-dev.html: it works from disk.
#   tools/emu.sh            build and open
#   tools/emu.sh --no-open  build only
set -e
cd "$(dirname "$0")/.."
REV=$(git rev-parse --short HEAD 2>/dev/null || echo dev)
[ -n "$(git status --porcelain firmware 2>/dev/null)" ] && REV="$REV+"   # (+: with changes not committed)
web/emu/build.sh dev
python3 web/emu/make_page.py --version "$REV"
[ "$1" = --no-open ] || open build/emu/drumtonic-emu-dev.html 2>/dev/null || echo "open build/emu/drumtonic-emu-dev.html in a browser"
