#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Send an SMK-37 or FM-1 the UBOOT soft key F0 22 24 35 7D F7, nothing else: it restarts into the chip's mask-ROM
bootloader (a USB disk "WL82 UBOOT1.00", 4C4A:8057), where jl-uboot-tool reads the flash. No flash is written; a
power cycle (or a short while with nobody talking to it) brings back what ran before.

The stock app understands the key too, on an 8-byte USB transfer on cable 0 (the port "SMK-37 Pro-Private"; checked
on v015 and v016, re/REPORT.md 10.4; the stock FM-1_015 on its port "FM-1", tried on a unit), as do Felucca-family
firmwares (Dinghy, Drumtonic) and their update loaders.

    python3 tools/backup/uboot_key.py           checks only: the port answers the identity query
    python3 tools/backup/uboot_key.py --send    sends the key and waits up to 20 s for the UBOOT disk

docs/BACKUP.md has the whole backup. Needs mido and python-rtmidi; the wait for the disk also pyusb and libusb.
"""
import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import fm1_install                                  # noqa: E402  (the identity query, the MIDI ports)

UBOOT_KEY = bytes([0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7])
UBOOT_ID = (0x4C4A, 0x8057)                         # the mask ROM's USB disk


def uboot_present():
    try:
        import usb.core
        import usb.backend.libusb1
    except ImportError:
        return None
    for lib in ('/opt/homebrew/lib/libusb-1.0.dylib', None):
        be = usb.backend.libusb1.get_backend(find_library=(lambda x, lib=lib: lib) if lib else None)
        if be:
            return usb.core.find(idVendor=UBOOT_ID[0], idProduct=UBOOT_ID[1], backend=be) is not None
    return None


ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
ap.add_argument('--send', action='store_true', help='send the UBOOT key (otherwise only check)')
ap.add_argument('--port', help='MIDI port (part of its name); default: the first SMK / Felucca-family port that answers '
                               '(the stock app answers on "SMK-37 Pro-Private", cable 0)')
a = ap.parse_args()

up = fm1_install.Updater(fm1_install.MidoBackend(), a.port)
dev = up.find()                                     # identity query F0 00 32 45 00 00 00 40 7F F7
if not dev:
    sys.exit('no SMK-37 or FM-1 answered%s: nothing sent' % (' on %r' % a.port if a.port else ''))
print('device: %s  port: %r%s' % (dev.id.text, dev.name, '  (update loader)' if dev.id.loader else ''))
if not a.send:
    dev.link.close()
    print('checks ok; would send %s (run with --send)' % UBOOT_KEY.hex(' ').upper())
    sys.exit(0)

dev.link.send(UBOOT_KEY)
dev.link.close()
print('sent %s' % UBOOT_KEY.hex(' ').upper())
for _ in range(40):
    time.sleep(0.5)
    if uboot_present():
        print('the UBOOT disk is there: usb:%04x:%04x' % UBOOT_ID)
        sys.exit(0)
if uboot_present() is None:
    print('(pyusb or libusb missing: cannot see the disk; look for "WL82 UBOOT1.00" yourself)')
    sys.exit(0)
sys.exit('no UBOOT disk after 20 s: power-cycle the keyboard (what ran before comes back)')
