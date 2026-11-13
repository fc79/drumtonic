#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The screenshots of the docs, from the firmware in this tree run on the computer (tests/run_host.sh):
#   tools/docs_screens.sh 0.1      -> docs/screens/*.png, the version shown as on a release
set -e
cd "$(dirname "$0")/.."
V="${1:-dev}"
rm -f build/host/dinghy-*.png
APP_VER="$V" sh tests/run_host.sh tests/scripts/drumtonic_fm1_docs.txt > /dev/null
mkdir -p docs/screens
shot() { cp "build/host/dinghy-$1.png" "docs/screens/$2.png"; echo "docs/screens/$2.png"; }
shot 27300 live
shot 29400 live-next
shot 3000 steps
shot 4700 step-held
shot 7300 sound
shot 16400 library
shot 19500 mix
shot 18200 lfo
shot 22100 delay
shot 24700 master
shot 26000 comp
shot 27900 guide
