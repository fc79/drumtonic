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
