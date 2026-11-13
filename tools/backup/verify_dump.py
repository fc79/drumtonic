#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check two flash dumps of an SMK-37 (or, with --fm1, an FM-1) before trusting them.

    python3 tools/backup/verify_dump.py [--fm1] dump-1.bin dump-2.bin [flash.bin]

- both exactly 1 MiB and byte-identical (SHA-256);
- "trimdat" at 0xC1224: the factory calibration record (keys, pads, wheels) is there (SMK-37 only: the FM-1 has no
  such record, and on a unit that ran stock FM-1_015 0xC0000-0xC1FFF was empty);
- with flash.bin (from the stock package of the firmware the unit ran, unpacked): how much of the app area matches.
  On a unit that ran stock v015, all but two sectors matched: 4 header entries (CRC and a flag bit) and a 32-byte
  record at the area's end, which the install writes.
"""
import hashlib
import sys
from pathlib import Path

args = sys.argv[1:]
fm1 = '--fm1' in args
if fm1:
    args.remove('--fm1')
if len(args) not in (2, 3):
    sys.exit(__doc__)
a, b = (Path(p).read_bytes() for p in args[:2])
ok = True

for name, d in zip(args[:2], (a, b)):
    print('%s  %d B  sha256 %s' % (name, len(d), hashlib.sha256(d).hexdigest()))
    ok &= len(d) == 0x100000
if a != b:
    print('FAIL: the two dumps differ')
    ok = False

if not fm1:
    tag = a[0xC1224:0xC1224 + 7]
    print('0xC1224: %r' % tag)
    ok &= tag == b'trimdat'

if len(args) == 3:
    ref = Path(args[2]).read_bytes()
    diff = [i for i in range(0, len(ref), 0x1000) if a[i:i + 0x1000] != ref[i:i + 0x1000]]
    print('flash.bin (0x0..0x%X): %d of %d sectors equal' % (len(ref) - 1, len(ref) // 0x1000 - len(diff), len(ref) // 0x1000))
    if diff:
        print('  differing sectors: ' + ' '.join('0x%05X' % i for i in diff[:16]) + (' ...' if len(diff) > 16 else ''))

print('OK: the dump can be trusted' if ok else 'FAIL: do not trust this dump, read again')
sys.exit(0 if ok else 1)
