#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/fm1_install.py --backup and --restore against the firmware itself: the FM-1 build on the host (tests/backup_bridge.c) runs
tests/scripts/drumtonic_fm1_backup.txt (two library sounds, then SAVE), then answers the backup's asks with backup.c.
Checks: every record pulled and checked (header, crc32) equals what the host saved; a save during the backup makes it
pull everything again; the backup file reads back the same (read_backup), and a damaged one is refused; a Drumtonic
without backup and another firmware are refused. The restore: a backup put on an FM-1 with nothing saved reads back the
same, an empty one empties every area, then the restart; HOME, 60 s without an answer, a format the FM-1 does not load
and another version's records: nothing written. Run from the repo root:
  python3 tests/backup_test.py"""
import io
import os
import queue
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fm1_install as I  # noqa: E402

I.DELAY.update(bk_reply=2.0)
failed = 0
SECTOR_MAX, SLOT_MAX, NBLOB, NSLOT = 4096 - 12, 4 * 4096 - 12, 8, 8


def ok(cond, what):
    global failed
    print(f"{what:<72} {'ok' if cond else 'FAIL'}")
    failed += not cond


class Bridge:
    """the host build, after its script: a MIDI link to its backup.c (decoded asks in, answers out)"""

    def __init__(self, exe, flash, script):
        env = dict(os.environ, HOST_FLASH=str(flash))
        self.p = subprocess.Popen([str(exe), str(flash.with_suffix("")), str(script)], stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE, text=True, env=env, cwd=ROOT)
        self.line()                             # "@@ ready": the script ran, its saves are in the flash file
        self.q, self.asks, self.save_after = queue.Queue(), 0, None
        self.answer = None                      # a restore's question: "yes", "home" or "tick 60001", at its first STATE
        self.drop = None                        # an ask whose answer is lost (cmd)
        self.flip = None                        # (id, off): that WRITE arrives with a byte changed

    def line(self):
        while True:
            s = self.p.stdout.readline()
            if not s:
                raise RuntimeError("the host build stopped")
            if s.startswith("@@ "):
                return s[3:].strip()

    def cmd(self, s):
        self.p.stdin.write(s + "\n")
        self.p.stdin.flush()
        return self.line()

    # the link fm1_install.py talks to
    lost = False

    def send(self, pkt):
        self.asks += 1
        if self.save_after is not None and self.asks == self.save_after:
            self.cmd("save")
        m = I.unpack7(pkt[1:-1])
        if m[2] == I.BK_STATE and self.answer:
            self.cmd(self.answer)
            self.answer = None
        if m[2] == I.BK_WRITE and self.flip == (m[8], int.from_bytes(m[9:13], "little")):
            m = bytearray(m)
            m[20] ^= 1
            m[-1] = ~sum(m[6:-1]) & 0xFF
        a = self.cmd(bytes(m).hex())
        if m[2] == self.drop:
            return True
        if a:
            self.q.put(b"\xF0" + I.pack7(bytes.fromhex(a)) + b"\xF7")
        return True

    def read(self, timeout):
        try:
            return self.q.get(timeout=max(timeout, 0))
        except queue.Empty:
            return None

    def drain(self):
        while not self.q.empty():
            self.q.get_nowait()

    def close(self):
        self.p.stdin.close()
        self.p.wait(10)


def saved(flash):
    """the host's saves (host.c host_flash): name -> the record's bytes after its header, None if not saved"""
    raw = flash.read_bytes()
    at = 0

    def take(n):
        nonlocal at
        b = raw[at:at + n]
        at += n
        return b
    sets = take(SECTOR_MAX + 4)
    blob = [take(SECTOR_MAX + 4) for _ in range(NBLOB)]
    slot = [take(SLOT_MAX + 4) for _ in range(NSLOT)]
    blobs = [take(NBLOB * 0x1000 + 4) for _ in range(NBLOB)]

    def rec(a):
        return a[4:4 + int.from_bytes(a[1:4], "little")] if a[0] else None
    return {"SETS": rec(sets), "KIT1": rec(blob[0]), "SONG": rec(blobs[1]), "LIB0": rec(slot[0]), "LIB1": rec(slot[1]),
            "KIT2": rec(slot[2])}


def main():
    tmp = Path(tempfile.mkdtemp(prefix="backup_test-"))
    exe = tmp / "bridge"
    subprocess.run(["cc", "-O2", "-Wall", "-Wno-unused-function", "-DDINGHY_HOST", '-DAPP_VER="0.0"', "-Ifirmware/hal",
                    "-Ifirmware/src", "-Ifirmware/gen", "-o", str(exe), "tests/backup_bridge.c"], cwd=ROOT, check=True)
    script = ROOT / "tests/scripts/drumtonic_fm1_backup.txt"
    ident = I.Identity("FM-1_987", "FM-1", 987, "0.0")

    # the whole backup, then the file
    flash = tmp / "a.flash"
    b = Bridge(exe, flash, script)
    seen = []
    recs = I.backup(I.Device(b, ident, "host"), lambda d, t: seen.append((d, t)))
    want = saved(flash)
    names = [r["name"] for r in recs]
    ok(names == ["SETS", "KIT1", "SONG", "LIB0", "LIB1", "KIT2"], f"the records: {' '.join(names)}")
    for r in recs:
        w = want.get(r["name"])
        ok((r["data"][12:] == w) if w is not None else r["data"] == b"",
           f"{r['name']}: {len(r['data'])} B, as saved" + ("" if w is not None else " (nothing saved: empty)"))
    ok(all(want[n] is not None for n in ("SETS", "KIT1", "SONG", "KIT2")) and (want["LIB0"] or want["LIB1"]),
       "the script saved the settings, both kit records, the patterns and the library")
    ok(seen and seen[-1][0] == seen[-1][1] == sum(len(r["data"]) for r in recs), "the progress reaches the total")
    raw = I.backup_bytes(recs, "FM-1_987", "0.0", "2026-11-16T12:00:00+0100")
    head, back = I.read_backup(raw)
    ok(head["device"] == "FM-1_987" and [(r["name"], r["data"]) for r in back] == [(r["name"], r["data"]) for r in recs],
       "the backup file reads back the same records")
    for what, bad in (("a byte changed", raw[:200] + bytes([raw[200] ^ 1]) + raw[201:]), ("cut short", raw[:-10]),
                      ("not a backup", b"hello" * 10)):
        try:
            I.read_backup(bad)
            ok(False, f"a damaged backup file is refused: {what}")
        except I.InstallError as e:
            ok(e.code == "badbackup", f"a damaged backup file is refused: {what}")
    rec = bytearray(raw)                        # a record damaged inside a file whose own crc32 is right
    k = rec.index(recs[0]["data"]) + 14            # (a data byte of SETS, after its 12-byte header)
    rec[k] ^= 1
    rec[-4:] = __import__("zlib").crc32(bytes(rec[:-4])).to_bytes(4, "little")
    try:
        I.read_backup(bytes(rec))
        ok(False, "a damaged record is refused (its crc32)")
    except I.InstallError as e:
        ok(e.code == "badbackup" and "SETS" in str(e), "a damaged record is refused (its crc32)")
    chunk, gen, info, proto = I.bk_info(b)              # a last chunk of one byte, and a refusal (past the end: None)
    sets = dict(info[0])
    ok(I.bk_pull(b, 19, gen, sets, lambda n: None) == recs[0]["data"] and sets["len"] % 19 == 1,
       "a record whose last chunk is one byte")
    sets["len"] += 1
    ok(I.bk_pull(b, chunk, gen, sets, lambda n: None) is None, "a READ past the record's end: refused, pulled again")
    b.close()

    # a save during the backup: pulled again from the start
    b = Bridge(exe, tmp / "b.flash", script)
    b.save_after = 5
    recs = I.backup(I.Device(b, ident, "host"))
    ok(b.asks > 2 * (5 + 1) and recs[0]["data"][12:] == saved(tmp / "b.flash")["SETS"],
       f"a save during the backup: everything pulled again ({b.asks} asks)")
    b.close()

    # the CLI: the file, and --check-backup
    b = Bridge(exe, tmp / "c.flash", script)

    class One:                                  # a backend with the bridge as its only port
        def input_names(self):
            return ["FM-1"]
        output_names = input_names

        def open(self, i, o):
            return b
    orig = I.handshake
    I.handshake = lambda link, tries=3: ident
    out = io.StringIO()
    path = tmp / "x.dtbak"
    rc = I.main(["--backup", str(path)], One(), out)
    ok(rc == 0 and path.exists() and I.read_backup(path.read_bytes())[0]["version"] == "0.0", "--backup FILE writes the file")
    rc = I.main(["--backup", str(path)], One(), io.StringIO())
    ok(rc == I.EXIT["exists"], "--backup refuses to overwrite a file")
    out = io.StringIO()
    rc = I.main(["--check-backup", str(path)], None, out)
    ok(rc == 0 and "KIT2" in out.getvalue(), "--check-backup lists the records")
    b.close()

    # refusals: another firmware, a Drumtonic without backup (no answer)
    class Silent:
        lost = False

        def send(self, pkt):
            return True

        def read(self, t):
            return None

        def drain(self):
            pass
    I.DELAY.update(bk_reply=0.05)
    for what, idt, code in (("another firmware", I.Identity("FM-1_015", "FM-1", 15), "model"),
                            ("a Drumtonic without backup", ident, "nobackup")):
        try:
            I.backup(I.Device(Silent(), idt, "x"))
            ok(False, f"refused: {what}")
        except I.InstallError as e:
            ok(e.code == code, f"refused: {what}")

    # the restore: a backup on an FM-1 with nothing saved, then read back
    b = Bridge(exe, tmp / "d0.flash", script)
    full = I.backup(I.Device(b, ident, "host"))
    b.close()
    empty_script = tmp / "empty.txt"
    empty_script.write_text("100 end\n")
    I.DELAY.update(bk_reply=2.0, bk_poll=0.0)
    b = Bridge(exe, tmp / "d.flash", empty_script)
    dev = I.Device(b, ident, "host")
    ok(all(not r["data"] for r in I.backup(dev)), "an FM-1 with nothing saved: every record empty")
    b.answer, steps, seen = "yes", [], []
    I.restore(dev, full, lambda d, t: seen.append((d, t)), steps.append)
    ok(steps == ["ask", "write"] and seen[-1][0] == seen[-1][1] == sum(len(r["data"]) for r in full),
       "the restore: the question, then the records written, all of them")
    ok(b.cmd("tick 400").split() == [str(I.BS_DONE), "1"], "the restore: then the FM-1 restarts")
    back = I.backup(dev)
    ok([(r["name"], r["data"]) for r in back] == [(r["name"], r["data"]) for r in full],
       "the restore: the FM-1 reads back every record as the backup had it")
    b.close()

    b = Bridge(exe, tmp / "d1.flash", empty_script)   # COMMIT's answer lost (the restart first): no failure
    dev = I.Device(b, ident, "host")
    b.answer, b.drop = "yes", I.BK_COMMIT
    I.DELAY.update(bk_write=0.3)
    ok(I.restore(dev, full) is False and b.cmd("tick 400").split()[1] == "1",
       "the restore: COMMIT's answer lost, not a failure (the FM-1 restarts)")
    b.close()
    b = Bridge(exe, tmp / "d2.flash", empty_script)   # a record changed on the way: refused, nothing after it
    dev = I.Device(b, ident, "host")
    b.answer, b.flip = "yes", (2, 0)
    try:
        I.restore(dev, full)
        ok(False, "the restore: a record changed on the way")
    except I.InstallError as e:
        st = I.bk_state(b)
        back = I.backup(dev)
        ok(e.code == "restore" and "not the one listed" in str(e) and st[0] == I.BS_FAIL and
           b.cmd("tick 400").split() == [str(I.BS_FAIL), "0"] and not I.restore_done(dev) and
           [r["data"] for r in back[:2]] == [r["data"] for r in full[:2]] and not any(r["data"] for r in back[2:]),
           "the restore: a record changed on the way: failed, no restart, the records before it written, none after")
    b.close()
    b = Bridge(exe, tmp / "d3.flash", empty_script)
    ok(I.restore_done(I.Device(b, ident, "host")), "after a restart: the restore's state idle")
    orig_info = I.bk_info
    I.bk_info = lambda link: orig_info(link)[:3] + (1,)
    try:
        I.restore(I.Device(b, ident, "host"), full)
        ok(False, "a Drumtonic with backup only: refused")
    except I.InstallError as e:
        ok(e.code == "norestore", "a Drumtonic with backup only: refused")
    I.bk_info = orig_info
    b.close()
    for what, edit in (("an unknown record", lambda h: h["records"][1].update(name="KIT9")),
                       ("a record twice", lambda h: h["records"][1].update(name="SETS"))):
        import json
        import zlib
        raw = I.backup_bytes(full, "FM-1_987", "0.0", "2026-11-17T12:00:00+0100")
        hl = int.from_bytes(raw[10:12], "little")
        h = json.loads(raw[12:12 + hl])
        edit(h)
        hb = json.dumps(h, separators=(",", ":")).encode()
        raw = raw[:10] + len(hb).to_bytes(2, "little") + hb + raw[12 + hl:-4]
        raw += zlib.crc32(raw).to_bytes(4, "little")
        try:
            I.read_backup(raw)
            ok(False, f"a backup file with {what}: refused")
        except I.InstallError as e:
            ok(e.code == "badbackup", f"a backup file with {what}: refused")

    b = Bridge(exe, tmp / "e.flash", script)       # an empty backup over saves: every area emptied
    dev = I.Device(b, ident, "host")
    b.answer = "yes"
    I.restore(dev, [dict(r, len=0, data=b"") for r in full])
    ok(all(not r["data"] for r in I.backup(dev)), "an empty backup: every area emptied")
    b.close()

    def refused(what, recs, answer, code, word=""):
        b = Bridge(exe, tmp / "f.flash", script)
        dev = I.Device(b, ident, "host")
        before = I.backup(dev)
        b.answer = answer
        try:
            I.restore(dev, recs)
            ok(False, f"the restore: {what}")
        except I.InstallError as e:
            ok(e.code == code and word in str(e) and [r["data"] for r in I.backup(dev)] == [r["data"] for r in before],
               f"the restore: {what}, nothing written")
        b.close()
    other = [dict(r, len=0, data=b"") for r in full]   # (written, it would empty every area)
    refused("HOME says no", other, "home", "cancelled")
    refused("60 s without an answer", other, "tick 60001", "cancelled")
    import zlib
    k = bytearray(full[1]["data"])                 # KIT1 in a format the FM-1 does not load (its crc32 right)
    k[12] ^= 0x55
    k[8:12] = zlib.crc32(bytes(k[12:])).to_bytes(4, "little")
    refused("a format it does not load", [full[0], dict(full[1], data=bytes(k))] + full[2:], None, "badbackup", "KIT1")
    refused("another version's records", full[:5], None, "badbackup", "another version")

    # the CLI: --restore
    class Restarts:                             # the FM-1 restarted: the host build again on the same flash
        def __init__(self):
            self.b = Bridge(exe, tmp / "g.flash", empty_script)

        def input_names(self):
            return ["FM-1"]
        output_names = input_names

        def open(self, i, o):
            if self.b.p.stdin.closed:
                self.b = Bridge(exe, tmp / "g.flash", empty_script)
            return self.b
    fm = Restarts()
    fm.b.answer = "yes"
    I.DELAY.update(reboot=0.0)
    out = io.StringIO()
    rc = I.main(["--restore", str(path)], fm, out)
    back = I.backup(I.Device(fm.open(0, 0), ident, "host"))
    ok(rc == 0 and "REPLACE ALL SAVES?" in out.getvalue() and "done" in out.getvalue() and
       [r["data"] for r in back] == [r["data"] for r in I.read_backup(path.read_bytes())[1]],
       "--restore FILE: done, and the FM-1 after its restart has the backup's saves")
    fm.b.close()
    bad = tmp / "other.dtbak"
    bad.write_bytes(I.backup_bytes(full, "FM-1_955", "0.0", "2026-11-17T12:00:00+0100"))
    rc = I.main(["--restore", str(bad)], One(), io.StringIO())
    ok(rc == I.EXIT["badbackup"], "--restore refuses another firmware's backup")
    I.handshake = orig
    shutil.rmtree(tmp, ignore_errors=True)
    print("FAILED" if failed else "all ok")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
