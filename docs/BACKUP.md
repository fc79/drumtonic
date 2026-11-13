# Backing up the FM-1's flash

Before any custom firmware goes on an FM-1, read its whole flash (1 MiB) and keep it. Some of it is in no package:
`key_mac` (0xFF000-0xFFFFF) and what other firmwares of the family saved in the shared places.

This is how it was done on an FM-1 (stock FM-1_015), from a Mac, **without writing anything** to it.

## What you need

- Python 3 with `mido python-rtmidi` (the MIDI side) and, on a Mac, `pyusb` plus libusb (`brew install libusb`).
- [jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool) (kagaimiq, MIT) with its requirements
  (`crcmod pyyaml pycryptodomex tqdm`).
- On a Mac, the adapter in `tools/backup/jl-uboot-tool-macos/`: jl-uboot-tool talks SCSI through Linux's SG_IO or
  Windows' SPTI; the adapter speaks USB Bulk-Only Transport over libusb instead. Into a checkout of jl-uboot-tool:

  ```sh
  cp tools/backup/jl-uboot-tool-macos/macport.py JLTOOL/scsiio/
  cp tools/backup/jl-uboot-tool-macos/mac_*.py JLTOOL/
  cd JLTOOL && patch -p1 < .../tools/backup/jl-uboot-tool-macos/jl-uboot-tool-macos.patch
  ```

  It needs `sudo` (macOS has to let go of the disk). Try it first on any USB stick, read only:
  `sudo python3 mac_selftest.py usb:VVVV:PPPP` (`python3 mac_selftest.py` lists the devices).

## Reading the flash

1. **The mask-ROM bootloader, without writing.** The stock firmware itself enters it on the UBOOT soft key
   `F0 22 24 35 7D F7`, sent as one 8-byte USB-MIDI transfer on cable 0, the FM-1's only port, "FM-1" (`uboot_key.py`
   finds it by itself). Drumtonic and its update loader honour it too. The ROM shows up as a USB disk `WL82 UBOOT1.00`
   (4C4A:8057) and **goes back to the firmware by itself after a short while** if nobody talks to it, so start the
   reader first:

   ```sh
   cd JLTOOL && sudo python3 jluboottool.py --device usb:4c4a:8057 --chip wl82 \
       "read 0 0x100000 dump-1.bin" "read 0 0x100000 dump-2.bin"
   ```

   It prints `Waiting for [usb:4c4a:8057]...`. Then, in another terminal:

   ```sh
   python3 tools/backup/uboot_key.py --send
   ```

   The reader loads its program into the chip's RAM (nothing in flash), prints the chip key (`0x980F`) and the flash
   id, and reads the flash twice (~2 s each). Only `read`: never `write`, `erase`, `erasechip` or `burnchipkey` here.

2. **Check the dumps:**

   ```sh
   python3 tools/backup/verify_dump.py --fm1 dump-1.bin dump-2.bin [flash.bin]
   ```

   Both must be 1,048,576 bytes and identical. With `flash.bin` unpacked from the stock package your FM-1 ran, it also
   shows how much of the app area matches (a few sectors differ: they hold what the install writes).

3. **Power-cycle** the FM-1: what ran before comes back. Keep the dump in two places.

The unit read here: chip key 0x980F, flash id **0x856014**. Used: 0x00000-0x93FFF (the app), 0xE9000-0xF1FFF and 0xFA000-0xFBFFF
(stock data); Drumtonic's store (settings 0xFC000, kit and patterns 0xF4000-0xF5FFF) was empty. Keep the whole dump
all the same: the FM-1's store is shared by the firmwares of its family, and other units may differ. Drumtonic's
sound library keeps the user's sounds at 0x97000 and 0x9B000, empty under the stock firmware (the stock data at
0xFA000-0xFBFFF is left alone).
