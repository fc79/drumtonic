# SPDX-License-Identifier: MIT (as kagaimiq/jl-uboot-tool, which it extends: copy it into a checkout of it, see docs/BACKUP.md)
"""
Read-only check of scsiio/macport.py on any USB mass storage device (a USB stick).

    python3 mac_selftest.py                      list the mass storage devices
    sudo python3 mac_selftest.py usb:VVVV:PPPP   TEST UNIT READY, INQUIRY, READ CAPACITY, READ(10) of LBA 0..7

Only these four commands are sent: nothing on the device is written.
"""
import hashlib, sys
import usb.util
from scsiio.macport import SCSIDev, find_devices
from scsiio.common import SCSIException

if len(sys.argv) < 2:
    for d, intf in find_devices():
        try:
            name = '%s %s' % (usb.util.get_string(d, d.iManufacturer), usb.util.get_string(d, d.iProduct))
        except Exception:
            name = '(name needs root)'
        print('usb:%04x:%04x  interface %d  %s' % (d.idVendor, d.idProduct, intf, name))
    sys.exit(0)

with SCSIDev(sys.argv[1]) as dev:
    print('opened', sys.argv[1])

    for i in range(3):                          # a fresh device may report UNIT ATTENTION first
        try:
            dev.execute(bytes(6), None, None)
            print('TEST UNIT READY  ok')
            break
        except SCSIException as e:
            print('TEST UNIT READY  not ready (%s)' % str(e).splitlines()[-1])

    data = bytearray(36)
    dev.execute(b'\x12\x00\x00\x00\x24\x00', None, data)
    print('INQUIRY          vendor %r product %r rev %r'
          % (bytes(data[8:16]).decode('ascii', 'replace').strip(),
             bytes(data[16:32]).decode('ascii', 'replace').strip(),
             bytes(data[32:36]).decode('ascii', 'replace').strip()))

    data = bytearray(8)
    dev.execute(b'\x25' + bytes(9), None, data)
    last, bs = int.from_bytes(data[:4], 'big'), int.from_bytes(data[4:], 'big')
    print('READ CAPACITY    %d blocks of %d B (%.1f GB)' % (last + 1, bs, (last + 1) * bs / 1e9))

    data = bytearray(8 * bs)
    resid = dev.execute(b'\x28\x00' + (0).to_bytes(4, 'big') + b'\x00' + (8).to_bytes(2, 'big') + b'\x00', None, data)
    print('READ(10) LBA 0-7 %d B, residue %d, sha256 %s' % (len(data), resid, hashlib.sha256(data).hexdigest()[:16]))

print('closed: the adapter works')
