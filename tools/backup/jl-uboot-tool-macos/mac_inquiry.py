# SPDX-License-Identifier: MIT (as kagaimiq/jl-uboot-tool, which it extends: copy it into a checkout of it, see docs/BACKUP.md)
"""
Send one SCSI INQUIRY to a device and print vendor / product / revision: nothing else.

    sudo python3 mac_inquiry.py usb:VVVV:PPPP

jluboottool.py takes the chip from the vendor field (and loads that chip's RAM loader),
so this shows which chip it would pick before anything is loaded.
"""
import sys, time
import usb.core
from scsiio.macport import SCSIDev

# wait for the device (UBOOT appears after the key and leaves on its own if nobody talks to it)
print('waiting for %s (up to 3 min)' % sys.argv[1], flush=True)
for _ in range(360):
    try:
        dev = SCSIDev(sys.argv[1])
        break
    except (FileNotFoundError, usb.core.USBError):    # (just enumerated: the driver may still hold it)
        time.sleep(0.5)
else:
    sys.exit('the device did not appear')

with dev:
    data = bytearray(36)
    dev.execute(b'\x12\x00\x00\x00\x24\x00', None, data)
    print('INQUIRY vendor %r product %r rev %r' % (bytes(data[8:16]).decode('ascii', 'replace').strip(),
                                                  bytes(data[16:32]).decode('ascii', 'replace').strip(),
                                                  bytes(data[32:36]).decode('ascii', 'replace').strip()))
