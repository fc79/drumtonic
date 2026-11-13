// SPDX-License-Identifier: GPL-3.0-only
// web/fm1ota.js and web/fm1pkg.js against a simulated FM-1 behind a fake MIDIAccess (no browser, no hardware):
// the device's ports go away and come back under new ids at each restart (running -> loader -> new firmware),
// a pulled cable sends "disconnected", replies can arrive in pieces. The device logic is tests/install_test.py's
// FakeFM1. Run from the repo root: node web/test_web.mjs [build/drumtonic.fwsc]
import { readFileSync, existsSync } from "node:fs";
import * as O from "./fm1ota.js";
import * as P from "./fm1pkg.js";

Object.assign(O.DELAY, { open: 5, start: 5, reply: 0, loader: 5, reboot: 5, retry: 10, poll: 20, hs: 60,
                         idleCheck: 200, idleWrite: 200, waitLoader: 1500, waitReboot: 1500 });
let failed = 0;
function ok(cond, what) {
    console.log(`${what.padEnd(78)} ${cond ? "ok" : "FAILED"}`);
    if (!cond) failed++;
}

// ------------------------------------------------------------ fake MIDI ---
let nextId = 1;
class FakeAccess {
    constructor() { this.inputs = new Map(); this.outputs = new Map(); this.ls = []; }
    addEventListener(t, f) { if (t === "statechange") this.ls.push(f); }
    removeEventListener(t, f) { this.ls = this.ls.filter(g => g !== f); }
    emit(port) { for (const f of [...this.ls]) f({ port }); }
}
class FakeInput {
    constructor(name) { this.id = "in" + nextId++; this.name = name; this.state = "connected"; this.onmidimessage = null; this.opened = false; }
    open() { return new Promise(r => setTimeout(() => { this.opened = true; r(this); }, 2)); }   // as Web MIDI: async
    deliver(bytes) { setTimeout(() => this.state === "connected" && this.opened && this.onmidimessage && this.onmidimessage({ data: Uint8Array.from(bytes) }), 0); }
}
class FakeOutput {
    constructor(name, dev) { this.id = "out" + nextId++; this.name = name; this.state = "connected"; this.dev = dev; this.sent = 0; this.opened = false; }
    open() { return new Promise(r => setTimeout(() => { this.opened = true; r(this); }, 2)); }
    send(data, when) {
        if (this.state !== "connected") throw new Error("InvalidStateError");
        if (!this.opened) { this.dev.early++; return; }      // sent before open() resolved: lost, as CoreMIDI may
        this.sent++;
        const d = Array.from(data), gen = this.dev.gen;
        setTimeout(() => { if (gen === this.dev.gen && this.dev.connected) this.dev.rx(d); },
                   when ? Math.max(0, when - performance.now()) : 0);
    }
}

const FIN = [O.FINISH_CHECK, O.FINISH_WRITE];
class FakeFM1 {
    constructor(access, image, o = {}) {
        Object.assign(this, { access, image, identity: "FM-1_015", name: "FM-1", unplugAfter: 0, stallAfter: 0, badAddr: null,
                              afterWrite: "FM-1_923", loader: ["ota-FM-1_923", "Felucca Update"], split: false, stuck: false }, o);
        this.served = this.bad = this.upgrades = this.hs = this.early = 0;
        this.gen = 0;
        this.boot(this.identity, this.name);
    }
    boot(identity, name) {
        this.unplug();
        this.gen++;
        this.identity = identity; this.name = name; this.connected = true; this.waiting = null; this.queue = [];
        if (this.stuck && identity.startsWith("ota-")) { this.connected = false; return; }
        this.inp = new FakeInput(name); this.out = new FakeOutput(name, this);
        this.access.inputs.set(this.inp.id, this.inp); this.access.outputs.set(this.out.id, this.out);
        this.access.emit(this.inp); this.access.emit(this.out);
    }
    unplug() {
        this.connected = false;
        for (const p of [this.inp, this.out]) {
            if (!p) continue;
            p.state = "disconnected";
            this.access.inputs.delete(p.id); this.access.outputs.delete(p.id);
            this.access.emit(p);
        }
    }
    tx(pkt) {
        if (this.split && pkt.length > 8) { this.inp.deliver(pkt.slice(0, 5)); this.inp.deliver(pkt.slice(5)); }
        else this.inp.deliver(pkt);
    }
    rx(d) {
        const eq = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);
        if (eq(d, O.HS_QUERY)) {
            this.hs++;
            const t = Array.from(this.identity, c => c.charCodeAt(0));
            this.tx([0xF0, ...O.pack7([0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)]), 0xF7]);
        } else if (eq(d, O.UPGRADE)) {
            this.upgrades++;
            const first = this.badAddr ?? 0;
            this.queue = this.identity.startsWith("ota-")
                ? [...Array.from({ length: 6 }, (_, k) => [first + k * 512, 512]), [O.FINISH_WRITE, 8]]
                : [[0, 64], [0x40, 160], [0x1000, 512], [O.FINISH_CHECK, 8]];
            this.next();
        } else if (this.waiting) {
            const u = O.unpack7(d.slice(1, -1));
            const [addr, n] = this.waiting;
            const want = FIN.includes(addr) ? Array.from("success\0", c => c.charCodeAt(0)) : Array.from(this.image.subarray(addr, addr + n));
            if (u.length !== 15 + want.length || want.some((x, i) => u[14 + i] !== x)) this.bad++;
            this.waiting = null;
            this.served++;
            if (this.unplugAfter && this.served >= this.unplugAfter) { this.unplug(); return; }
            if (this.stallAfter && this.served >= this.stallAfter) return;
            if (addr === O.FINISH_CHECK) setTimeout(() => this.boot(...this.loader), 50);
            else if (addr === O.FINISH_WRITE) setTimeout(() => this.boot(this.afterWrite, "X0X FM-1"), 50);
            else this.next();
        }
    }
    next() {
        if (!this.queue.length) return;
        const [addr, n] = this.waiting = this.queue.shift();
        const u = [0, 0x59, 0x30, 0, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, n & 0xFF, n >> 8, 0];
        u.push(~u.slice(6, 14).reduce((a, b) => a + b, 0) & 0xFF);
        this.tx([0xF0, ...O.pack7(u), 0xF7]);
    }
}

// --------------------------------------------------------------- helpers ---
const image = Uint8Array.from({ length: 0x2000 }, (_, i) => (i * 7) & 0xFF);
async function run(o, product = "FM-1_923") {
    const access = new FakeAccess(), dev = new FakeFM1(access, image, o), up = new O.Updater(access), steps = [];
    let err = null, found = null;
    try {
        found = await up.find();
        if (!found) throw new O.InstallError("notfound", "FM-1 not found");
        await up.install(found, image, product, (k, a) => steps.push(k + (a !== undefined ? ":" + a : "")));
    } catch (e) { err = e; }
    return { dev, err, steps, found, access };
}
const t0 = () => performance.now();

// ----------------------------------------------------------------- tests ---
{
    const data = [0, 1, 0x7F, 0x80, 0xFF, 0x55, 0xAA, 3, 9];
    ok(O.unpack7(O.pack7(data)).slice(0, data.length).every((x, i) => x === data[i]), "pack7 / unpack7 round trip");
    const r = O.parseRequest([0xF0, ...O.pack7((() => { const u = [0, 0x59, 0x30, 0, 0, 0, 1, 0x34, 0x12, 0, 0, 0, 2, 0]; u.push(~u.slice(6, 14).reduce((a, b) => a + b, 0) & 0xFF); return u; })()), 0xF7]);
    ok(r && r.addr === 0x1234 && r.n === 512 && r.fl === 1, "parseRequest: address, length, flags");
    const idp = (b) => [0xF0, ...O.pack7([0, 0x59, 0x11, 0, 0, 0, ...b, ...new Array(28 - b.length).fill(0)]), 0xF7];
    const bytes = t => Array.from(t, c => c.charCodeAt(0));
    let id = O.parseIdentity(idp(bytes("FM-1_923\0V0.2")));
    ok(id && id.text === "FM-1_923" && id.version === 923 && id.release === "0.2", "parseIdentity: the version after the identity");
    id = O.parseIdentity(idp(bytes("FM-1_923")));
    ok(id && id.text === "FM-1_923" && id.release === "", "parseIdentity: no version sent -> none");
    const flash = bytes("FM-1_923"); flash.length = 24; flash.fill(0, 8); flash.push(0x85, 0x60, 0x14);
    id = O.parseIdentity(idp(flash));
    ok(id && id.release === "", "parseIdentity: the flash id at 24-26 is not a version");
    id = O.parseIdentity(idp(bytes("FM-1_923\0V0.123456789012345")));
    ok(id && id.text === "FM-1_923" && id.release === "", "parseIdentity: a version running into byte 24 -> none");
    ok(O.sameKeyboard("FM-1_015", "FM-1_923") && !O.sameKeyboard("SMK-37 Pro_923", "FM-1_923") && O.OWN_LOADER_RE.test("ota-FM-1_923")
       && !O.OWN_LOADER_RE.test("ota-FM-1_015"), "models and own loaders as in fm1_install.py");
}
{
    const { dev, err, steps } = await run({});
    ok(!err && dev.bad === 0 && dev.early === 0 && dev.upgrades === 2 && dev.served === 11 && steps.at(-1) === "done:FM-1_923" && steps.includes("write:100"),
       `install: running -> loader -> Drumtonic (${dev.served} reads, ${err ? err.message : "no error"})`);
}
{
    const { dev, err } = await run({ split: true });
    ok(!err && dev.bad === 0 && dev.served === 11, "install: every reply in two pieces -> rebuilt, installed");
}
{
    const { dev, err, steps } = await run({ identity: "ota-FM-1_923", name: "Felucca Update" });
    ok(!err && dev.bad === 0 && dev.upgrades === 1 && !steps.includes("loader"), "install: already in update mode -> finishes the write");
}
{
    const { dev, err } = await run({ identity: "SMK-37 Pro_923", name: "SMK-37 Pro" });
    ok(err && err.code === "model" && dev.upgrades === 0, "an SMK-37 -> refused, nothing sent but the handshake");
}
{
    const { dev, err } = await run({ identity: "ota-FM-1_015", name: "FM-1 Update" });
    ok(err && err.code === "foreign" && dev.upgrades === 0, "another firmware's update loader -> refused, nothing sent");
}
{
    const { dev, err } = await run({ name: "IAC Driver Bus 1" });
    ok(err && err.code === "notfound" && dev.hs === 0 && dev.out.sent === 0, "a port of other gear -> not probed, FM-1 not found");
}
{
    const s = t0(), { err } = await run({ unplugAfter: 2 });
    ok(err && err.code === "lost" && /nothing was written/.test(err.message) && t0() - s < 1000, "unplugged in step 1 -> lost at once, nothing written");
}
{
    const s = t0(), { err } = await run({ identity: "ota-FM-1_923", name: "Felucca Update", unplugAfter: 3 });
    ok(err && err.code === "lost" && t0() - s < 150, `unplugged during the write -> lost at once (${Math.round(t0() - s)} ms, the idle time is 200)`);
}
{
    const { err } = await run({ identity: "ota-FM-1_923", name: "Felucca Update", stallAfter: 3 });
    ok(err && err.code === "stopped", "the loader stops answering -> stopped after the idle time");
}
{
    const { err } = await run({ identity: "ota-FM-1_923", name: "Felucca Update", badAddr: image.length });
    ok(err && err.code === "badreq", "a read past the package -> refused");
}
{
    const { err } = await run({ afterWrite: "FM-1_015" });
    ok(err && err.code === "mismatch" && /FM-1_015/.test(err.message), "another identity after the restart -> mismatch");
}
{
    const { err } = await run({ stuck: true });
    ok(err && err.code === "noloader", "no loader after step 1 -> noloader");
}

// the real package: what the installer page embeds
const pkg = process.argv[2] || "build/drumtonic.fwsc";
if (existsSync(pkg)) {
    const raw = new Uint8Array(readFileSync(pkg));
    let r = null, e = null;
    try { r = P.loadPackage(raw, "FM-1_923"); } catch (x) { e = x; }
    ok(r && r.product === "FM-1_923" && !e, `${pkg}: FM-1_923, loader marker, CRCs (${e ? e.message : r.image.length + " B image"})`);
    const bad = raw.slice(); bad[raw.length - 100] ^= 1;
    let e2 = null; try { P.loadPackage(bad, "FM-1_923"); } catch (x) { e2 = x; }
    ok(e2 && /damaged/.test(e2.message), "a changed byte in the package -> damaged, refused");
    let e3 = null; try { P.loadPackage(raw, "FM-1_955"); } catch (x) { e3 = x; }
    ok(e3 && /not FM-1_955/.test(e3.message), "another identity wanted -> refused");
} else console.log(`${pkg}: skipped (not built)`);

if (failed) { console.log(`${failed} web test(s) FAILED`); process.exit(1); }
console.log("web tests passed");
