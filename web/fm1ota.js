// SPDX-License-Identifier: GPL-3.0-only
// The FM-1 update over Web MIDI: the same steps, checks and refusals as tools/fm1_install.py. Step 1, the running
// firmware reads parts of the package and starts the update loader; step 2, the loader reads the whole package and
// writes it; then the FM-1 restarts and its identity is checked. In both steps the device asks (SysEx read requests
// on the logical image) and we answer. `access` is a MIDIAccess (navigator.requestMIDIAccess({sysex: true})) or the
// fake in web/test_web.mjs. No DOM: tools/make_installer.py inlines this file into the installer page.

export const HS_QUERY = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
export const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];
export const FINISH_CHECK = 0xE0000000, FINISH_WRITE = 0xF0000000, MAXDATA = 512;
// never probe other gear: the FM-1 and the SMK-37 family under their firmwares' names, and the update loaders
export const PORT_RE = /fm-1|smk|elite|mke-p37|starry|felucca|sloop|clipper|melodee|fomni|x0x|makkuro|maku|rainbow|snoop|vector|vox|ota|composite|sinco|usb-midi/i;
// a Felucca-family update loader: the only one whose write we finish
export const OWN_LOADER_RE = /^ota-(FM-1|SMK-37 Pro|SMK-37 Elite|MKE-P37)_9\d\d$/i;

// milliseconds; the tests shorten them
export const DELAY = { open: 300, start: 2000, reply: 10, loader: 3000, reboot: 3000, retry: 1000, poll: 1000, hs: 1000,
                       idleCheck: 8000, idleWrite: 180000, waitLoader: 30000, waitReboot: 40000 };

export class InstallError extends Error {
    constructor(code, message) { super(message); this.code = code; }
}

// ------------------------------------------------------------ wire format ---

export function pack7(data) {
    const out = [];
    let acc = 0, nb = 0;
    for (const b of data) {
        acc |= b << nb; nb += 8;
        while (nb >= 7) { out.push(acc & 0x7F); acc >>>= 7; nb -= 7; }
    }
    if (nb) out.push(acc & 0x7F);
    return out;
}

export function unpack7(s) {
    const out = [];
    let acc = 0, nb = 0;
    for (const b of s) {
        acc |= (b & 0x7F) << nb; nb += 7;
        while (nb >= 8) { out.push(acc & 0xFF); acc >>>= 8; nb -= 8; }
    }
    return out;
}

const sum = (a, from, to) => { let s = 0; for (let i = from; i < to; i++) s += a[i]; return s; };

export function response(addr, data, fl = 0) {
    const n = data.length;
    const body = [0x00, 0x59, 0x30, (n + 8) & 0xFF, ((n + 8) >> 8) & 0xFF, 0, fl,
                  addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF,
                  n & 0xFF, (n >> 8) & 0xFF, 0];
    for (let i = 0; i < n; i++) body.push(data[i]);
    body.push(~sum(body, 6, body.length) & 0xFF);
    return [0xF0, ...pack7(body), 0xF7];
}

/** -> {fl, addr, n} or null */
export function parseRequest(pkt) {
    if (pkt.length < 2 || pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
    const u = unpack7(pkt.slice(1, -1));
    if (u.length !== 15 || u[0] !== 0 || u[1] !== 0x59 || u[2] !== 0x30 || (~sum(u, 6, 14) & 0xFF) !== u[14]) return null;
    return { fl: u[6], addr: (u[7] | u[8] << 8 | u[9] << 16 | u[10] << 24) >>> 0, n: u[11] | u[12] << 8 | u[13] << 16 };
}

/** -> {text, model, version, loader, release} or null. version: the identity's number; release: the firmware's own
 *  version, which Drumtonic sends after the identity's 0 ('V', the text, a 0, before byte 24), else "" */
export function parseIdentity(pkt) {
    if (pkt.length < 2 || pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
    const d = unpack7(pkt.slice(1, -1));
    if (d.length !== 34 || d[0] !== 0 || d[1] !== 0x59 || d[2] !== 0x11) return null;
    let text = "", i = 6;
    for (; i < 33 && d[i]; i++) text += String.fromCharCode(d[i]);
    let release = "";
    if (d[i + 1] === 0x56) {                    /* 'V' */
        let j = i + 2, r = "";
        for (; j < 30 && d[j] >= 0x20 && d[j] < 0x7F; j++) r += String.fromCharCode(d[j]);
        if (j < 30 && d[j] === 0) release = r;
    }
    const m = /^([^_]+)_(\d+)$/.exec(text);
    return m ? { text, model: m[1], version: +m[2], loader: m[1].toLowerCase().startsWith("ota-"), release } : null;
}

export const modelOf = text => text.split("_")[0].replace(/^ota-/, "");
const SMK_FAMILY = /^(SMK-37[ +].*|MKE-P37|STARRYKEY-37.*)$/i;
/** the models of identities a and b are the same hardware (never an FM-1 and an SMK) */
export function sameKeyboard(a, b) {
    const ma = modelOf(a), mb = modelOf(b);
    return ma === mb || (SMK_FAMILY.test(ma) && SMK_FAMILY.test(mb));
}

// ------------------------------------------------------------------ MIDI ---

const sleep = ms => new Promise(r => setTimeout(r, ms));
const now = () => (typeof performance !== "undefined" ? performance.now() : Date.now());
const ports = map => { const a = []; map.forEach(p => a.push(p)); return a; };

/** one MIDI in/out pair with a SysEx queue; lost once either port disconnects or a send fails */
class Link {
    constructor(access, input, output) {
        this.access = access; this.input = input; this.output = output;
        this.q = []; this.waiter = null; this.buf = null; this.lost = false; this.closed = false;
        this.onmsg = ev => this.rx(ev.data);
        this.onstate = ev => {
            const p = ev.port;
            if (p && (p.id === input.id || p.id === output.id) && p.state === "disconnected") this.lose();
        };
        input.onmidimessage = this.onmsg;
        if (access.addEventListener) access.addEventListener("statechange", this.onstate);
    }
    rx(data) {
        // rebuild F0 ... F7 even when it comes in pieces; real-time bytes and other messages are dropped
        for (const b of data) {
            if (b >= 0xF8) continue;
            if (b === 0xF0) this.buf = [b];
            else if (this.buf) {
                this.buf.push(b);
                if (b === 0xF7) { this.push(this.buf); this.buf = null; }
                else if (b & 0x80) this.buf = null;
            }
        }
    }
    push(pkt) {
        if (this.waiter) { const w = this.waiter; this.waiter = null; w(pkt); } else this.q.push(pkt);
    }
    lose() {
        this.lost = true;
        if (this.waiter) { const w = this.waiter; this.waiter = null; w(null); }
    }
    send(pkt, delay = 0) {
        if (this.lost || this.input.state === "disconnected" || this.output.state === "disconnected") { this.lose(); return false; }
        try {
            // the delay is the device's timestamp, not a timer: a hidden tab throttles timers, not MIDI
            if (delay) this.output.send(pkt, now() + delay); else this.output.send(pkt);
            return true;
        } catch (e) { this.lose(); return false; }
    }
    /** the next SysEx, or null after ms (or at once when lost) */
    read(ms) {
        if (this.q.length) return Promise.resolve(this.q.shift());
        if (this.lost) return Promise.resolve(null);
        return new Promise(resolve => {
            const t = setTimeout(() => { if (this.waiter === done) this.waiter = null; resolve(null); }, Math.max(0, ms));
            const done = pkt => { clearTimeout(t); resolve(pkt); };
            this.waiter = done;
        });
    }
    drain() { this.q = []; this.buf = null; }
    close() {
        if (this.closed) return;
        this.closed = true;
        if (this.input.onmidimessage === this.onmsg) this.input.onmidimessage = null;
        if (this.access.removeEventListener) this.access.removeEventListener("statechange", this.onstate);
    }
}

function pairOutput(name, outs) {
    const exact = outs.find(o => o.name === name);
    if (exact) return exact;
    const base = s => s.replace(/\s+\d+$/, "");        // Windows numbers each port
    const same = outs.filter(o => base(o.name) === base(name));
    if (same.length === 1) return same[0];
    return outs.length === 1 ? outs[0] : null;
}

async function handshake(link, tries = 3) {
    link.drain();
    for (let k = 0; k < tries; k++) {
        if (!link.send(HS_QUERY)) return null;
        const end = now() + DELAY.hs;
        let left;
        while ((left = end - now()) > 0) {
            const p = await link.read(left);
            if (!p) break;
            const ident = parseIdentity(p);
            if (ident) return ident;
        }
    }
    return null;
}

// --------------------------------------------------------------- updater ---

export class Updater {
    constructor(access) { this.access = access; }

    /** the names of the MIDI inputs, all of them (for the "not found" message) */
    inputNames() { return ports(this.access.inputs).map(p => p.name); }

    /** the first matching port that answers the handshake (and want(identity)): {link, id, name} or null */
    async find(want = null) {
        const outs = ports(this.access.outputs).filter(o => o.state !== "disconnected");
        for (const input of ports(this.access.inputs)) {
            if (input.state === "disconnected" || !PORT_RE.test(input.name)) continue;
            const output = pairOutput(input.name, outs);
            if (!output) continue;
            try { await input.open?.(); await output.open?.(); } catch (e) { continue; }
            const link = new Link(this.access, input, output);
            await sleep(DELAY.open);    // CoreMIDI may drop a message sent just after opening
            const id = await handshake(link, 2);
            if (id && (!want || want(id))) return { link, id, name: input.name };
            link.close();
        }
        return null;
    }

    async waitFor(want, ms) {
        const end = now() + ms;
        while (now() < end) {
            const dev = await this.find(want);
            if (dev) return dev;
            await sleep(DELAY.retry);
        }
        return null;
    }

    /** answer read requests until the device asks for `finish`; -> {served, finished, lost} */
    async serve(link, image, finish, idle, progress) {
        let served = 0, last = now();
        for (;;) {
            const pkt = await link.read(DELAY.poll);
            if (!pkt) {
                if (link.lost) return { served, finished: false, lost: true };
                if (now() - last > idle) return { served, finished: false, lost: false };
                continue;
            }
            const r = parseRequest(pkt);
            if (!r) continue;
            last = now();
            if (r.addr === FINISH_CHECK || r.addr === FINISH_WRITE) {
                link.send(response(r.addr, Array.from("success\0", c => c.charCodeAt(0))));
                if (r.addr === finish) return { served, finished: true, lost: false };
                continue;
            }
            if (r.n > MAXDATA || r.addr + r.n > image.length)
                throw new InstallError("badreq", `the device asked for 0x${r.addr.toString(16)}+${r.n}, outside the package`);
            if (!link.send(response(r.addr, image.subarray(r.addr, r.addr + r.n), r.fl), DELAY.reply))
                return { served, finished: false, lost: true };
            served++;
            progress(served, r.addr + r.n);
        }
    }

    /** step 1: the running firmware checks the package and starts the loader */
    async check(dev, image, step) {
        step("start", dev.id.text);
        dev.link.send(UPGRADE);
        await sleep(DELAY.start);
        const { served, finished, lost } = await this.serve(dev.link, image, FINISH_CHECK, DELAY.idleCheck,
                                                            k => step("check", k));
        dev.link.close();
        if (!finished)
            throw new InstallError(lost ? "lost" : "stopped",
                `the FM-1 ${lost ? "was disconnected" : "stopped answering"} after ${served} requests; nothing was written`);
        step("loader");
        await sleep(DELAY.loader);
        const ota = await this.waitFor(i => i.loader, DELAY.waitLoader);
        if (!ota) throw new InstallError("noloader", "the update loader did not appear");
        return ota;
    }

    /** step 2: the loader reads and writes the whole package, then restarts */
    async write(ota, image, step) {
        step("write", 0);
        ota.link.send(UPGRADE);
        await sleep(DELAY.start);
        const { served, finished, lost } = await this.serve(ota.link, image, FINISH_WRITE, DELAY.idleWrite,
            (k, end) => step("write", Math.min(99, Math.floor(end * 100 / image.length))));
        ota.link.close();
        if (!finished)
            throw new InstallError(lost ? "lost" : "stopped",
                `the loader ${lost ? "was disconnected" : "stopped answering"} after ${served} requests`);
        step("write", 100);
    }

    async verify(product, step) {
        step("reboot");
        await sleep(DELAY.reboot);
        const back = await this.waitFor(i => !i.loader, DELAY.waitReboot);
        if (!back) throw new InstallError("noreturn", "the FM-1 did not come back");
        back.link.close();
        if (back.id.text !== product) throw new InstallError("mismatch", `written, but the FM-1 reports ${back.id.text} (expected ${product})`);
        step("done", back.id.text);
        return back.id.text;
    }

    /** the refusals before anything is sent: -> null, or an InstallError (the link is then closed) */
    refuse(dev, product) {
        let e = null;
        if (!sameKeyboard(dev.id.text, product))
            e = new InstallError("model", `the device is ${dev.id.text}, the package is for ${product}`);
        else if (dev.id.loader && !OWN_LOADER_RE.test(dev.id.text))
            e = new InstallError("foreign", `the FM-1 is in the update mode of another firmware (${dev.id.text})`);
        if (e) dev.link.close();
        return e;
    }

    /** running firmware -> loader -> new firmware; dev from find(). A device already in update mode only needs the write */
    async install(dev, image, product, step = () => {}) {
        const e = this.refuse(dev, product);
        if (e) throw e;
        const ota = dev.id.loader ? dev : await this.check(dev, image, step);
        await this.write(ota, image, step);
        return this.verify(product, step);
    }
}

// ---------------------------------------------------------------- backup ---
// Drumtonic's saves over MIDI, as tools/fm1_install.py --backup / --restore (firmware/src/backup.c has the wire
// format): the computer pulls every record as the flash holds it, [magic][length][crc32] then the bytes, and keeps
// them in one .dtbak file:
//   "DTBACKUP", format u16 (1), header length u16, the header (UTF-8 JSON: device, version, proto, date, and the
//   records: id, name, len, in id order), the records' bytes one after the other, then the zlib crc32 of all before
// A record of len 0 is an empty area (a restore leaves it empty). A restore sends the list (BEGIN), the FM-1 asks
// REPLACE ALL SAVES? on its screen (SEL yes, HOME no, 60 s: no), then takes every record (WRITE) and COMMIT: it
// empties what the backup had empty and restarts. The file is byte for byte the one fm1_install.py writes.

export const DRUMTONIC_ID = "FM-1_987";
export const BK = { INFO: 0x60, READ: 0x61, DONE: 0x62, BEGIN: 0x63, STATE: 0x64, WRITE: 0x65, COMMIT: 0x66, ABORT: 0x67 };
export const BS = { IDLE: 0, ASK: 1, YES: 2, NO: 3, FAIL: 4, DONE: 5 };
const BK_TAG = [0x44, 0x42];                   // 'D' 'B'
const BK_FILE_MAGIC = "DTBACKUP", BK_FILE_FORMAT = 1;
const BK_STORE = 0x3733544E, BK_BLOBS = 0x53424C42, BK_SLOT = 0x544F4C53;
export const BK_MAGIC = { SETS: BK_STORE, KIT1: BK_STORE, SONG: (BK_BLOBS ^ BK_STORE) >>> 0,
                          LIB0: (BK_SLOT ^ BK_STORE) >>> 0, LIB1: (BK_SLOT ^ BK_STORE) >>> 0, KIT2: (BK_SLOT ^ BK_STORE) >>> 0 };
const BK_STATUS = { 1: "no such record", 2: "past the record's end", 3: "the flash failed", 4: "a bad ask" };
const BK_WSTATUS = { 2: "not the bytes it expected", 3: "the flash failed", 4: "a bad ask",
                     6: "not now (it stopped waiting: 10 s without a message?)", 7: "the record is not the one listed" };
Object.assign(DELAY, { bkReply: 1000, bkWrite: 5000, bkPoll: 250, bkAsk: 65000 });

const bkU32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
const bkLE = (v, n) => Array.from({ length: n }, (_, i) => (v >>> (8 * i)) & 0xFF);
let crcTable = null;
/** zlib's crc32 */
export function crc32(data, c = 0) {
    if (!crcTable) crcTable = Array.from({ length: 256 }, (_, n) => { let x = n; for (let k = 0; k < 8; k++) x = x & 1 ? 0xEDB88320 ^ (x >>> 1) : x >>> 1; return x >>> 0; });
    c = ~c >>> 0;
    for (let i = 0; i < data.length; i++) c = crcTable[(c ^ data[i]) & 0xFF] ^ (c >>> 8);
    return ~c >>> 0;
}

export function bkMessage(cmd, body) {
    const n = body.length;
    return [0xF0, ...pack7([0x00, 0x59, cmd, n & 0xFF, (n >> 8) & 0xFF, (n >> 16) & 0xFF, ...body, ~sum(body, 0, n) & 0xFF]), 0xF7];
}

/** an answer to cmd: its body (from the tag on), or null */
export function bkParse(pkt, cmd) {
    if (pkt.length < 2 || pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
    const u = unpack7(pkt.slice(1, -1));
    if (u.length < 9 || u[0] !== 0 || u[1] !== 0x59 || u[2] !== cmd) return null;
    const n = u[3] | u[4] << 8 | u[5] << 16;
    if (u.length < 7 + n) return null;
    const body = u.slice(6, 6 + n);
    if ((~sum(body, 0, n) & 0xFF) !== u[6 + n] || body[0] !== BK_TAG[0] || body[1] !== BK_TAG[1]) return null;
    return body;
}

/** what is wrong with a record's bytes (header, length, crc32), or null */
export function bkRecordCheck(name, rec) {
    if (rec.length < 12) return "shorter than its header";
    if (name in BK_MAGIC && bkU32(rec, 0) !== BK_MAGIC[name]) return "not Drumtonic's (magic)";
    if (bkU32(rec, 4) + 12 !== rec.length) return "its length";
    if (crc32(rec.subarray(12)) !== bkU32(rec, 8)) return "its crc32";
    return null;
}

/** one ask, its answer's body (tag on); null if no valid answer after tries (each waits `wait` ms, or bkReply) */
async function bkAsk(link, cmd, body, tries = 3, wait = null) {
    for (let k = 0; k < tries; k++) {
        link.drain();
        if (!link.send(bkMessage(cmd, body))) throw new InstallError("lost", "the FM-1 was disconnected");
        const end = now() + (wait ?? DELAY.bkReply);
        let left;
        while ((left = end - now()) > 0) {
            const p = await link.read(left);
            if (!p) break;
            const a = bkParse(p, cmd);
            if (a) return a;
        }
        if (link.lost) throw new InstallError("lost", "the FM-1 was disconnected");
    }
    return null;
}

/** -> {chunk, gen, recs: [{id, name, cap, len}], proto} */
export async function bkInfo(link) {
    const a = await bkAsk(link, BK.INFO, BK_TAG);
    if (!a) throw new InstallError("nobackup", "this Drumtonic has no backup over MIDI: install a newer one first");
    if (a.length < 10 || (a[2] !== 1 && a[2] !== 2))
        throw new InstallError("nobackup", `this Drumtonic's backup speaks protocol ${a[2] ?? "?"}, this page 1 and 2`);
    const chunk = a[3] | a[4] << 8, gen = bkU32(a, 5), count = a[9];
    if (a.length !== 10 + 16 * count || chunk < 1 || chunk > MAXDATA) throw new InstallError("badreq", "the FM-1's backup table is not valid");
    const recs = [];
    for (let r = 0; r < count; r++) {
        const e = a.slice(10 + 16 * r, 26 + 16 * r);
        const rec = { id: e[0], name: String.fromCharCode(...e.slice(2, 6)), cap: bkU32(e, 6), len: bkU32(e, 10) };
        if (rec.id !== r || rec.len > rec.cap || !!(e[1] & 1) !== !!rec.len || (rec.len && rec.len < 12))
            throw new InstallError("badreq", `the FM-1's backup table is not valid (record ${r})`);
        recs.push(rec);
    }
    return { chunk, gen, recs, proto: a[2] };
}

/** a record's bytes, a chunk at a time; null if a save came in between (gen changed) */
async function bkPull(link, chunk, gen, rec, progress) {
    const out = new Uint8Array(rec.len);
    for (let off = 0; off < rec.len;) {
        const n = Math.min(chunk, rec.len - off);
        const a = await bkAsk(link, BK.READ, [...BK_TAG, rec.id, ...bkLE(off, 4), ...bkLE(n, 2)]);
        if (!a) throw new InstallError("stopped", `the FM-1 stopped answering during the backup (${rec.name} at ${off})`);
        if (a.length < 14 || a[2] !== rec.id || bkU32(a, 3) !== off)          // (a refusal: 14 bytes)
            throw new InstallError("badreq", `a wrong answer during the backup (${rec.name} at ${off})`);
        if (bkU32(a, 10) !== gen) return null;
        if (a[9]) {
            if (a[9] === 2) return null;        // past the record's end: a save changed it (pull everything again)
            throw new InstallError("badreq", `the FM-1 refused ${rec.name} at ${off}: ${BK_STATUS[a[9]] ?? a[9]}`);
        }
        if ((a[7] | a[8] << 8) !== n || a.length !== 14 + n) throw new InstallError("badreq", `a wrong answer during the backup (${rec.name} at ${off})`);
        out.set(a.slice(14), off);
        off += n;
        progress(n);
    }
    return out;
}

/** every record of the Drumtonic on dev.link -> [{id, name, len, data}]; pulled again from the start if a save lands
 *  meanwhile (up to tries times). progress(done, total) in bytes */
export async function backup(dev, progress = () => {}, tries = 3) {
    if (dev.id.text !== DRUMTONIC_ID) throw new InstallError("model", `the device is ${dev.id.text}: a backup needs Drumtonic (${DRUMTONIC_ID})`);
    const link = dev.link;
    for (let t = 0; t < tries; t++) {
        const { chunk, gen, recs } = await bkInfo(link);
        const total = recs.reduce((s, r) => s + r.len, 0);
        let done = 0, again = false;
        for (const r of recs) {
            r.data = r.len ? await bkPull(link, chunk, gen, r, n => { done += n; progress(done, total); }) : new Uint8Array(0);
            if (!r.data) { again = true; break; }
            const bad = r.len && bkRecordCheck(r.name, r.data);
            if (bad) {
                await bkAsk(link, BK.DONE, [...BK_TAG, 1]);
                throw new InstallError("badreq", `the FM-1 sent ${r.name} damaged: ${bad}`);
            }
        }
        if (again || (await bkInfo(link)).gen !== gen) continue;   // (a SAVE writes several records: again)
        await bkAsk(link, BK.DONE, [...BK_TAG, 0]);
        return recs;
    }
    await bkAsk(link, BK.DONE, [...BK_TAG, 1]);
    throw new InstallError("stopped", "the FM-1 kept saving during the backup: stop editing on it and try again");
}

/** the local time as fm1_install.py writes it: 2026-11-17T14:30:00+0100 */
export function bkDate(d = new Date()) {
    const p = (v, n = 2) => String(v).padStart(n, "0"), o = -d.getTimezoneOffset();
    return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}T${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}` +
           `${o < 0 ? "-" : "+"}${p(Math.floor(Math.abs(o) / 60))}${p(Math.abs(o) % 60)}`;
}

/** the backup file's bytes */
export function backupBytes(recs, device, version, date) {
    const head = new TextEncoder().encode(JSON.stringify({ device, version, proto: 1, date,
        records: recs.map(r => ({ id: r.id, name: r.name, len: r.data.length })) }));
    const n = 12 + head.length + recs.reduce((s, r) => s + r.data.length, 0);
    const out = new Uint8Array(n + 4);
    out.set(Array.from(BK_FILE_MAGIC, c => c.charCodeAt(0)), 0);
    out.set([...bkLE(BK_FILE_FORMAT, 2), ...bkLE(head.length, 2)], 8);
    out.set(head, 12);
    let at = 12 + head.length;
    for (const r of recs) { out.set(r.data, at); at += r.data.length; }
    out.set(bkLE(crc32(out.subarray(0, n)), 4), n);
    return out;
}

/** a backup file's bytes -> {head, recs: [{id, name, len, data}]}; InstallError("badbackup") if anything is off */
export function readBackup(raw) {
    const bad = why => new InstallError("badbackup", `not a Drumtonic backup, or a damaged one: ${why}`);
    if (raw.length < 16 || String.fromCharCode(...raw.subarray(0, 8)) !== BK_FILE_MAGIC) throw bad("no DTBACKUP at the start");
    if (crc32(raw.subarray(0, raw.length - 4)) !== bkU32(raw, raw.length - 4)) throw bad("its crc32");
    const fmt = raw[8] | raw[9] << 8, hl = raw[10] | raw[11] << 8;
    if (fmt !== BK_FILE_FORMAT) throw new InstallError("badbackup", `a backup of format ${fmt}: this page reads format ${BK_FILE_FORMAT}`);
    let head, at = 12 + hl;
    const recs = [];
    try {
        head = JSON.parse(new TextDecoder("utf-8", { fatal: true }).decode(raw.subarray(12, 12 + hl)));
        for (const r of head.records) {
            const n = Number(r.len);
            if (!Number.isInteger(n) || n < 0) throw new Error(`a length of ${r.len}`);
            recs.push({ id: Number(r.id), name: String(r.name), len: n, data: raw.slice(at, at + n) });
            at += n;
        }
    } catch (e) { throw bad(`its header (${e.message})`); }
    if (at !== raw.length - 4) throw bad("its length");
    if (recs.some((r, i) => r.id !== i) || new Set(recs.map(r => r.name)).size !== recs.length) throw bad("its list of records");
    for (const r of recs) {
        if (!(r.name in BK_MAGIC)) throw bad(`its list of records (${r.name})`);
        const why = r.len && bkRecordCheck(r.name, r.data);
        if (why) throw bad(`${r.name}: ${why}`);
    }
    return { head, recs };
}

/** the restore's state on the FM-1 (BS) and the records written (bit id): {state, written}, or null if no answer */
export async function bkState(link) {
    const a = await bkAsk(link, BK.STATE, BK_TAG);
    return a && a.length === 4 ? { state: a[2], written: a[3] } : null;
}
async function bkAbort(link) { try { await bkAsk(link, BK.ABORT, BK_TAG, 1); } catch (e) { /* lost */ } }

/** put a backup's records (readBackup) back on the Drumtonic on dev.link: the list first, the FM-1 asks REPLACE ALL
 *  SAVES? (step("ask")), then every record (step("write"), progress(done, total)), then COMMIT: the FM-1 restarts.
 *  -> true: COMMIT answered; false: no answer to it (the FM-1 may have restarted first: see restoreDone).
 *  InstallError("cancelled") if HOME said no or 60 s went by; abort() (the page's Cancel) while asking: "cancelled" */
export async function restore(dev, recs, progress = () => {}, step = () => {}, abort = { asked: false }) {
    if (dev.id.text !== DRUMTONIC_ID) throw new InstallError("model", `the device is ${dev.id.text}: a restore needs Drumtonic (${DRUMTONIC_ID})`);
    const link = dev.link;
    const { chunk, recs: info, proto } = await bkInfo(link);
    if (proto < 2) throw new InstallError("norestore", "this Drumtonic makes backups but cannot take one back: install a newer one first");
    if (recs.map(r => r.name).join(" ") !== info.map(r => r.name).join(" "))
        throw new InstallError("badbackup", `this backup's records (${recs.map(r => r.name).join(" ")}) are not this Drumtonic's ` +
                                            `(${info.map(r => r.name).join(" ")}): a backup of another version`);
    const body = [...BK_TAG, recs.length];
    recs.forEach((r, i) => {
        const d = r.data;
        if (d.length > info[i].cap) throw new InstallError("badbackup", `the backup's ${r.name} (${d.length} B) does not fit this Drumtonic's (${info[i].cap} B)`);
        body.push(r.id, 0, 0, 0, ...bkLE(d.length, 4), ...(d.length ? d.subarray(8, 12) : [0, 0, 0, 0]),
                  ...(d.length >= 16 ? d.subarray(12, 16) : [0, 0, 0, 0]));
    });
    const a = await bkAsk(link, BK.BEGIN, body, 1);   // (once: a second BEGIN while asking would be "busy")
    if (!a) {
        const st = await bkState(link);
        if (!st || st.state !== BS.ASK) throw new InstallError("stopped", "the FM-1 did not answer the restore");
    } else if (a.length !== 4) throw new InstallError("badreq", "a wrong answer to the restore");
    else if (a[2] === 4) {
        const e = new InstallError("refused", `the FM-1 refused the backup's ${recs[a[3]]?.name ?? "?"}: not a format this Drumtonic loads`);
        e.record = recs[a[3]]?.name;
        throw e;
    } else if (a[2] === 5) throw new InstallError("busy", "the FM-1 is busy with another restore: wait a minute, then try again");
    else if (a[2]) throw new InstallError("badreq", `the FM-1 refused the restore (status ${a[2]})`);
    step("ask");
    const end = now() + DELAY.bkAsk;
    for (;;) {                                  // the question: SEL yes, HOME no (60 s: no)
        if (abort.asked) { await bkAbort(link); throw new InstallError("cancelled", "cancelled on this page: nothing was written"); }
        const st = await bkState(link);
        if (!st) throw new InstallError("stopped", "the FM-1 stopped answering");
        if (st.state === BS.YES) break;
        if (st.state === BS.NO) throw new InstallError("cancelled", "the FM-1 said no: nothing was written");
        if (st.state !== BS.ASK || now() > end) {
            await bkAbort(link);
            throw new InstallError("stopped", `the FM-1 is not asking any more (state ${st.state}): nothing was written`);
        }
        await sleep(DELAY.bkPoll);
    }
    step("write");
    const total = recs.reduce((s, r) => s + r.data.length, 0);
    let done = 0;
    try {
        for (const r of recs) {
            const d = r.data;
            for (let off = 0; off < d.length; off += chunk) {
                const n = Math.min(chunk, d.length - off), last = off + n === d.length;
                const w = await bkAsk(link, BK.WRITE, [...BK_TAG, r.id, ...bkLE(off, 4), ...bkLE(n, 2), ...d.subarray(off, off + n)], 1,
                                      last ? DELAY.bkWrite : null);   // (the last chunk: after the flash write)
                if (!w) throw new InstallError("stopped", `the FM-1 stopped answering during the restore (${r.name} at ${off})`);
                if (w.length !== 11 || w[2] !== r.id || bkU32(w, 3) !== off || (w[7] | w[8] << 8) !== n)
                    throw new InstallError("badreq", `a wrong answer during the restore (${r.name} at ${off})`);
                if (w[9]) throw new InstallError("failed", `the FM-1 refused ${r.name} at ${off}: ${BK_WSTATUS[w[9]] ?? w[9]}`);
                done += n;
                progress(done, total);
            }
        }
    } catch (e) {
        if (e.code !== "lost") await bkAbort(link);   // (nothing more is written; the FM-1 goes on as it was)
        throw e;
    }
    let c;                                      // COMMIT empties what the backup had empty (erases), then restarts:
    try { c = await bkAsk(link, BK.COMMIT, BK_TAG, 1, DELAY.bkWrite); }   // an answer lost to the restart is no failure
    catch (e) { return false; }
    if (!c) return false;
    if (c.length !== 3 || c[2]) {
        await bkAbort(link);
        throw new InstallError("failed", "the FM-1 could not finish the restore: " + ({ 2: "a record missing", 3: "the flash failed" }[c[2]] ?? "a wrong answer"));
    }
    return true;
}

/** after the restart: the FM-1 back, its restore state idle (a fresh start); not restarted, it would read 2 or 4 */
export async function restoreDone(dev) {
    const st = await bkState(dev.link);
    return !!st && st.state === BS.IDLE;
}
