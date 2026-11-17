#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare a Drumtonic backup (tools/fm1_install.py --backup) with a full flash dump of the same FM-1 (docs/BACKUP.md),
taken with nothing saved in between: each record must be, byte for byte, what the flash holds at its place.

    python3 tools/backup/backup_vs_dump.py BACKUP.dtbak DUMP.bin

An empty record (len 0) must be an area with no valid record of its kind (wrong magic, or a length past it)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import fm1_install as I  # noqa: E402

AT = {"SETS": (0xFC000, 0x1000), "KIT1": (0xF4000, 0x1000), "SONG": (0xF5000, 0x5000),   # firmware/src/store.h, FM-1
      "LIB0": (0x97000, 0x4000), "LIB1": (0x9B000, 0x4000), "KIT2": (0x9F000, 0x4000)}


def main(argv):
    if len(argv) != 2:
        sys.exit(__doc__)
    try:
        head, recs = I.read_backup(Path(argv[0]).read_bytes())
    except I.InstallError as e:
        sys.exit(f"error: {e}")
    dump = Path(argv[1]).read_bytes()
    if len(dump) != 0x100000:
        sys.exit(f"error: {argv[1]} is {len(dump)} B, not a 1 MiB dump")
    print(f"backup of {head['device']} version {head.get('version') or '?'}, {head['date']}")
    bad = 0
    for r in recs:
        at, cap = AT.get(r["name"], (None, 0))
        if at is None:
            print(f"  {r['name']}: not known here")
            bad += 1
            continue
        if r["len"]:
            same = dump[at:at + r["len"]] == r["data"]
            print(f"  {r['name']}  {r['len']:6d} B at {at:#07x}: {'same' if same else 'DIFFERENT'}")
        else:
            h = dump[at:at + 12]
            n = int.from_bytes(h[4:8], "little")
            same = int.from_bytes(h[0:4], "little") != I.BK_MAGIC[r["name"]] or n + 12 > cap or \
                I.bk_record_check(r["name"], dump[at:at + 12 + n]) is not None
            print(f"  {r['name']}  empty    at {at:#07x}: {'no record there either' if same else 'DIFFERENT: the flash holds one'}")
        bad += not same
    print("the backup is what the flash holds" if not bad else f"{bad} record(s) differ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
