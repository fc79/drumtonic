// SPDX-License-Identifier: GPL-3.0-only
// web/fm1ota.js's backup and restore against the firmware itself: the FM-1 build on the host (tests/backup_bridge.c,
// as tests/backup_test.py uses it) behind a fake MIDIAccess. A COMMIT restarts it: its ports go, and it comes back on the
// same flash. Checks: a backup as the FM-1 holds it; the file byte for byte as tools/fm1_install.py writes it (and
// --check-backup reads it); a damaged file refused; a restore read back after the restart; HOME, a record changed on
// the way, a format the FM-1 does not load, a Drumtonic without backup. Run from the repo root:
//   node web/test_backup.mjs
import { execFileSync, spawn } from "node:child_process";
import { mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createInterface } from "node:readline";
import * as O from "./fm1ota.js";

Object.assign(O.DELAY, { open: 5, hs: 300, retry: 20, reboot: 5, waitReboot: 3000, bkReply: 2000, bkWrite: 2000, bkPoll: 0 });
let failed = 0;
function ok(cond, what) {
    console.log(`${what.padEnd(78)} ${cond ? "ok" : "FAILED"}`);
    if (!cond) failed++;
}

const tmp = mkdtempSync(join(process.env.TMPDIR || tmpdir(), "web_backup-"));
const exe = join(tmp, "bridge");
execFileSync("cc", ["-O2", "-Wall", "-Wno-unused-function", "-DDINGHY_HOST", '-DAPP_VER="0.0"', "-Ifirmware/hal", "-Ifirmware/src",
                    "-Ifirmware/gen", "-o", exe, "tests/backup_bridge.c"]);
const SCRIPT = "tests/scripts/drumtonic_fm1_backup.txt", EMPTY = join(tmp, "empty.txt");
writeFileSync(EMPTY, "100 end\n");

/** the host build after its script: decoded asks in, answers out (tests/backup_bridge.c) */
class Bridge {
    constructor(flash, script) {
        this.p = spawn(exe, [flash.replace(/\.flash$/, ""), script], { env: { ...process.env, HOST_FLASH: flash }, stdio: ["pipe", "pipe", "inherit"] });
        this.lines = []; this.wait = null;
        createInterface({ input: this.p.stdout }).on("line", s => {
            if (!s.startsWith("@@ ")) return;
            if (this.wait) { const w = this.wait; this.wait = null; w(s.slice(3).trim()); } else this.lines.push(s.slice(3).trim());
        });
        this.ready = this.line();
    }
    line() { return this.lines.length ? Promise.resolve(this.lines.shift()) : new Promise(r => { this.wait = r; }); }
    cmd(s) { this.p.stdin.write(s + "\n"); return this.line(); }
    close() { return new Promise(r => { this.p.on("exit", r); this.p.stdin.end(); }); }
}

const hex = a => Buffer.from(a).toString("hex");
let nextId = 1;
/** a MIDIAccess with one FM-1 on it: the bridge answers backup.c's asks; the identity is FM-1_987 version 0.0 */
class FakeFM1 {
    constructor(flash, script, o = {}) {
        Object.assign(this, { flash, answer: null, flip: null, drop: null, silent: false }, o);
        this.inputs = new Map(); this.outputs = new Map(); this.ls = []; this.chain = Promise.resolve();
        this.boot(script);
    }
    addEventListener(t, f) { if (t === "statechange") this.ls.push(f); }
    removeEventListener(t, f) { this.ls = this.ls.filter(g => g !== f); }
    boot(script) {
        this.b = new Bridge(this.flash, script);
        const dev = this;
        this.inp = { id: "in" + nextId++, name: "FM-1", state: "connected", onmidimessage: null, open: async () => {} };
        this.out = { id: "out" + nextId++, name: "FM-1", state: "connected", open: async () => {},
                     send(data) { if (this.state !== "connected") throw new Error("InvalidStateError"); const d = Array.from(data); dev.chain = dev.chain.then(() => dev.rx(d, this)); } };
        this.inputs.set(this.inp.id, this.inp); this.outputs.set(this.out.id, this.out);
    }
    tx(inp, pkt) { setTimeout(() => inp.state === "connected" && inp.onmidimessage && inp.onmidimessage({ data: Uint8Array.from(pkt) }), 0); }
    async rx(d, out) {
        const inp = this.inp;
        if (out !== this.out) return;
        await this.b.ready;
        if (d.length === O.HS_QUERY.length && d.every((x, i) => x === O.HS_QUERY[i])) {
            const t = Array.from("FM-1_987\0V0.0", c => c.charCodeAt(0));
            return this.tx(inp, [0xF0, ...O.pack7([0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)]), 0xF7]);
        }
        if (this.silent) return;
        let m = O.unpack7(d.slice(1, -1));
        if (m[2] === O.BK.STATE && this.answer) { await this.b.cmd(this.answer); this.answer = null; }
        if (m[2] === O.BK.WRITE && this.flip && m[8] === this.flip[0] && (m[9] | m[10] << 8) === this.flip[1]) {
            m[20] ^= 1;
            let s = 0; for (let i = 6; i < m.length - 1; i++) s += m[i];
            m[m.length - 1] = ~s & 0xFF;
        }
        const a = await this.b.cmd(hex(m));
        if (m[2] === this.drop) return;
        if (a) this.tx(inp, [0xF0, ...O.pack7(Array.from(Buffer.from(a, "hex"))), 0xF7]);
        if (m[2] === O.BK.COMMIT && a && Buffer.from(a, "hex")[8] === 0) await this.restart();
    }
    async restart() {                           // 0.3 s later the FM-1 restarts: the ports go, it comes back on its flash
        await new Promise(r => setTimeout(r, 50));   // (the answer leaves first)
        const t = (await this.b.cmd("tick 400")).split(" ");
        this.restarted = t[1] === "1";
        for (const p of [this.inp, this.out]) { p.state = "disconnected"; this.inputs.delete(p.id); this.outputs.delete(p.id); for (const f of [...this.ls]) f({ port: p }); }
        await this.b.close();
        await new Promise(r => setTimeout(r, 30));
        this.boot(EMPTY);
    }
    close() { return this.b.close(); }
}

async function connect(fm) {
    await fm.b.ready;                           // (the script's saves made)
    const dev = await new O.Updater(fm).find();
    if (!dev) throw new Error("the fake FM-1 was not found");
    return dev;
}
const datas = recs => recs.map(r => hex(r.data));

// the backup, the file
let fm = new FakeFM1(join(tmp, "a.flash"), SCRIPT);
let dev = await connect(fm);
const seen = [];
const full = await O.backup(dev, (d, t) => seen.push([d, t]));
ok(full.map(r => r.name).join(" ") === "SETS KIT1 SONG LIB0 LIB1 KIT2" && full.every(r => r.len === r.data.length) &&
   ["SETS", "KIT1", "SONG", "KIT2"].every(n => full.find(r => r.name === n).data.length > 12),
   `the backup: ${full.map(r => `${r.name} ${r.data.length}`).join(", ")}`);
ok(seen.at(-1)[0] === seen.at(-1)[1] && seen.at(-1)[1] === full.reduce((s, r) => s + r.len, 0), "the progress reaches the total");
const date = "2026-11-17T14:30:00+0100";
const raw = O.backupBytes(full, "FM-1_987", "0.0", date);
const back = O.readBackup(raw);
ok(back.head.device === "FM-1_987" && back.head.date === date && hex(datas(back.recs).join("")) === hex(datas(full).join("")), "the file reads back the same records");
const recsJson = join(tmp, "recs.json"), pyOut = join(tmp, "py.dtbak"), jsOut = join(tmp, "js.dtbak");
writeFileSync(recsJson, JSON.stringify(full.map(r => ({ id: r.id, name: r.name, len: r.len, data: hex(r.data) }))));
writeFileSync(jsOut, raw);
execFileSync("python3", ["-c", `import json, sys; sys.path.insert(0, "tools"); import fm1_install as I
recs = [dict(r, data=bytes.fromhex(r["data"])) for r in json.load(open(sys.argv[1]))]
open(sys.argv[2], "wb").write(I.backup_bytes(recs, "FM-1_987", "0.0", sys.argv[3]))`, recsJson, pyOut, date]);
ok(hex(execFileSync("cat", [pyOut])) === hex(raw), "the file is byte for byte the one fm1_install.py writes");
ok(/KIT2/.test(execFileSync("python3", ["tools/fm1_install.py", "--check-backup", jsOut]).toString()), "fm1_install.py --check-backup reads it");
for (const [what, bad] of [["a byte changed", (() => { const b = raw.slice(); b[200] ^= 1; return b; })()], ["cut short", raw.slice(0, -10)],
                           ["not a backup", new TextEncoder().encode("hello".repeat(10))]]) {
    let e = null; try { O.readBackup(bad); } catch (x) { e = x; }
    ok(e && e.code === "badbackup", `a damaged file is refused: ${what}`);
}
dev.link.close(); await fm.close();

// the restore onto an FM-1 with nothing saved, read back after the restart
fm = new FakeFM1(join(tmp, "b.flash"), EMPTY, { answer: "yes" });
dev = await connect(fm);
const steps = [], prog = [];
const up = new O.Updater(fm);
let sure = await O.restore(dev, back.recs, (d, t) => prog.push([d, t]), s => steps.push(s));
dev.link.close();
let again = await up.waitFor(i => i.text === O.DRUMTONIC_ID, O.DELAY.waitReboot);
ok(sure && fm.restarted && steps.join(" ") === "ask write" && prog.at(-1)[0] === prog.at(-1)[1], "the restore: the question, every record, COMMIT, the restart");
ok(again && await O.restoreDone(again), "after the restart: the FM-1 back, its restore state idle");
ok(again && hex(datas(await O.backup(again)).join("")) === hex(datas(full).join("")), "the FM-1 after its restart has the backup's saves");
again?.link.close(); await fm.close();

// the refusals: nothing written
async function refused(what, recs, o, code, check = () => true) {
    const fm = new FakeFM1(join(tmp, "c.flash"), SCRIPT, o), dev = await connect(fm);
    const before = datas(await O.backup(dev));
    let e = null; try { await O.restore(dev, recs); } catch (x) { e = x; }
    const after = datas(await O.backup(dev));
    ok(e && e.code === code && check(e, before, after), `the restore: ${what} (${e ? e.code : "no error"})`);
    dev.link.close(); await fm.close();
}
const empty = full.map(r => ({ ...r, len: 0, data: new Uint8Array(0) }));     // (written, it would empty every area)
const same = (e, a, b) => a.join() === b.join();
await refused("HOME says no, nothing written", empty, { answer: "home" }, "cancelled", same);
await refused("60 s without an answer, nothing written", empty, { answer: "tick 60001" }, "cancelled", same);
const k = full[1].data.slice(); k[12] ^= 0x55; k.set([0, 1, 2, 3].map(i => (O.crc32(k.subarray(12)) >>> (8 * i)) & 0xFF), 8);
await refused("a format it does not load, nothing written", [full[0], { ...full[1], data: k }, ...full.slice(2)], {}, "refused",
              (e, a, b) => e.record === "KIT1" && same(e, a, b));
await refused("another version's records, nothing written", full.slice(0, 5), {}, "badbackup", same);
{   // a record changed on the way: failed, no restart, the records before it written, none after
    const fm = new FakeFM1(join(tmp, "d.flash"), EMPTY, { answer: "yes", flip: [2, 0] }), dev = await connect(fm);
    let e = null; try { await O.restore(dev, full); } catch (x) { e = x; }
    const st = await O.bkState(dev.link), now = await O.backup(dev);
    ok(e && e.code === "failed" && /not the one listed/.test(e.message) && st.state === O.BS.FAIL && !fm.restarted &&
       datas(now).slice(0, 2).join() === datas(full).slice(0, 2).join() && now.slice(2).every(r => !r.data.length),
       "the restore: a record changed on the way, failed: the records before it written, none after");
    dev.link.close(); await fm.close();
}
{   // COMMIT's answer lost to the restart: not a failure
    const fm = new FakeFM1(join(tmp, "e.flash"), EMPTY, { answer: "yes", drop: O.BK.COMMIT }), dev = await connect(fm);
    Object.assign(O.DELAY, { bkWrite: 300 });
    const r = await O.restore(dev, full);
    ok(r === false, "the restore: COMMIT's answer lost, not a failure (false: check after the restart)");
    Object.assign(O.DELAY, { bkWrite: 2000 });
    dev.link.close(); await fm.close();
}
{   // a Drumtonic without backup: no answer to INFO
    const fm = new FakeFM1(join(tmp, "f.flash"), EMPTY, { silent: true }), dev = await connect(fm);
    Object.assign(O.DELAY, { bkReply: 50 });
    let e = null; try { await O.backup(dev); } catch (x) { e = x; }
    ok(e && e.code === "nobackup", "a Drumtonic without backup: refused");
    Object.assign(O.DELAY, { bkReply: 2000 });
    dev.link.close(); await fm.close();
}

rmSync(tmp, { recursive: true, force: true });
if (failed) { console.log(`${failed} backup web test(s) FAILED`); process.exit(1); }
console.log("backup web tests passed");
