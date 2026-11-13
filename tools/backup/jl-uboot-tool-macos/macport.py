# SPDX-License-Identifier: MIT (as kagaimiq/jl-uboot-tool, which it extends: copy it into a checkout of it, see docs/BACKUP.md)
from scsiio.common import *
import struct
import usb.core, usb.util, usb.backend.libusb1

#
# macOS has no SG_IO and doesn't let user code send raw CDBs to a disk the
# mass storage driver owns, so here we take the interface from the driver
# (libusb needs root for that) and speak USB Bulk-Only Transport ourselves.
#
# The device path is 'usb:VVVV:PPPP' or 'usb:VVVV:PPPP:N' (the N-th such device), hex IDs.
#

LIBUSB = '/opt/homebrew/lib/libusb-1.0.dylib'

CBW_SIG = 0x43425355    # 'USBC'
CSW_SIG = 0x53425355    # 'USBS'

def find_backend():
    return usb.backend.libusb1.get_backend(find_library=lambda x: LIBUSB)

def find_devices(vid=None, pid=None):
    """ All USB mass storage (bulk-only) devices, optionally filtered by IDs """
    found = []
    for d in usb.core.find(find_all=True, backend=find_backend()):
        if vid is not None and (d.idVendor, d.idProduct) != (vid, pid):
            continue
        for cfg in d:
            for intf in cfg:
                if (intf.bInterfaceClass, intf.bInterfaceProtocol) == (0x08, 0x50):
                    found.append((d, intf.bInterfaceNumber))
    return found


class SCSIDev(SCSIDevBase):
    """
    SCSI Generic I/O device implementation for macOS: USB Bulk-Only Transport over libusb
    """

    def open(self, path):
        parts = path.split(':')
        if len(parts) not in (3, 4) or parts[0] != 'usb':
            raise ValueError("device path must be 'usb:VVVV:PPPP[:N]', got %r" % path)

        vid, pid = int(parts[1], 16), int(parts[2], 16)
        nth = int(parts[3]) if len(parts) == 4 else 0

        devs = find_devices(vid, pid)
        if len(devs) <= nth:
            raise FileNotFoundError('no mass storage device %04x:%04x [%d]' % (vid, pid, nth))

        self.dev, self.intf = devs[nth]
        self.detached = False

        if self.dev.is_kernel_driver_active(self.intf):
            self.dev.detach_kernel_driver(self.intf)     # (root)
            self.detached = True

        usb.util.claim_interface(self.dev, self.intf)

        intf = self.dev.get_active_configuration()[(self.intf, 0)]
        self.ep_out = usb.util.find_descriptor(intf, custom_match=lambda e:
            usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT)
        self.ep_in = usb.util.find_descriptor(intf, custom_match=lambda e:
            usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN)

        self.tag = 0
        self.is_open = True

    def close(self):
        if self.is_open:
            usb.util.release_interface(self.dev, self.intf)
            if self.detached:
                try:
                    self.dev.attach_kernel_driver(self.intf)
                except usb.core.USBError:
                    pass
            usb.util.dispose_resources(self.dev)
            self.is_open = False

    def reset_recovery(self):
        """ Bulk-Only Mass Storage Reset, then clear both halts (BOT spec 5.3.4) """
        self.dev.ctrl_transfer(0x21, 0xFF, 0, self.intf, None)
        self.dev.clear_halt(self.ep_in)
        self.dev.clear_halt(self.ep_out)

    def transfer(self, cdb, data_out, data_in, timeout=5000):
        """ One CBW / data / CSW exchange; returns (status, residue) """
        self.tag = (self.tag + 1) & 0xFFFFFFFF

        dlen = len(data_out) if data_out else len(data_in) if data_in else 0
        flags = 0x80 if data_in else 0x00

        cbw = struct.pack('<IIIBBB', CBW_SIG, self.tag, dlen, flags, 0, len(cdb)) + cdb.ljust(16, b'\x00')
        self.ep_out.write(cbw, timeout)

        try:
            if data_out:
                self.ep_out.write(data_out, timeout)
            elif data_in:
                got = self.ep_in.read(len(data_in), timeout)
                data_in[:len(got)] = bytes(got)
        except usb.core.USBError as e:
            if e.errno != 32:   # a stall ends the data phase, the CSW still follows
                raise
            self.dev.clear_halt(self.ep_in if data_in else self.ep_out)

        try:
            csw = bytes(self.ep_in.read(13, timeout))
        except usb.core.USBError as e:
            if e.errno != 32:
                raise
            self.dev.clear_halt(self.ep_in)
            csw = bytes(self.ep_in.read(13, timeout))

        sig, tag, residue, status = struct.unpack('<IIIB', csw)
        if sig != CSW_SIG or tag != self.tag or status == 2:
            self.reset_recovery()
            raise SCSIException('Transfer failed: bad CSW %s (phase error or out of sync)' % csw.hex('.'))

        return status, residue

    def execute(self, cdb, data_out, data_in, max_sense_len=32, return_sense_buffer=False):
        if data_out and data_in:
            raise NotImplemented('Indirect I/O is not supported')

        status, residue = self.transfer(cdb, data_out, data_in)

        sense = b''
        if status == 1:
            sense = bytearray(max_sense_len)
            self.transfer(bytes([0x03, 0, 0, 0, max_sense_len, 0]), None, sense)
            raise SCSIException('Transfer failed: command failed\nSense looks like this:\n'
                                + bytes(sense).hex('.'))

        if return_sense_buffer:
            return residue, sense
        else:
            return residue
