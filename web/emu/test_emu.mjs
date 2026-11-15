// SPDX-License-Identifier: GPL-3.0-only
// The emulator plays what the FM-1 build on this computer plays, bit for bit: each FM-1 script in tests/scripts runs
// on tests/host.c (built here with cc) and on build/emu/drumtonic-emu.wasm (web/emu/build.sh) through emu-core.js,
// the same input at the same milliseconds; the two sounds must be equal and every "expect" must hold in both.
// Run from the repo root, after web/emu/build.sh:  node web/emu/test_emu.mjs [script ...]
import { readFileSync, readdirSync, mkdtempSync, existsSync } from "node:fs";
import { execFileSync, spawnSync } from "node:child_process";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { EmuCore } from "./emu-core.js";

const WASM = "build/emu/drumtonic-emu.wasm";
if (!existsSync(WASM)) { console.log(`no ${WASM}: run web/emu/build.sh first`); process.exit(1); }
const bytes = readFileSync(WASM);
const tmp = mkdtempSync(join(tmpdir(), "emu-test-"));
const host = join(tmp, "dinghy");
// the reference: tests/run_host.sh's build, without fused multiply-adds: WebAssembly has none, and a compiler that
// targets a CPU with them (gcc on Ubuntu 26.04's runners) fuses fx.c's float maths, which changes the last bits
execFileSync("cc", ["-O2", "-ffp-contract=off", "-Wall", "-Wno-unused-function", "-DDINGHY_HOST", '-DAPP_VER="0.0"',
    "-Ifirmware/hal", "-Ifirmware/src", "-Ifirmware/gen", "-o", host, "firmware/src/dinghy.c"], { stdio: "inherit" });

// tests/host.c's SCRIPT0 (no script file), as a script
const SCRIPT0 = `200 keys 0x80\n700 keys 0x4880\n1500 keys 0\n1700 midi 0x90 81 120\n2300 midi 0x80 81 0\n2500 btn 1
2700 keys 0x204\n3400 keys 0\n`;

const cint = s => { const m = /^([+-]?)(0x[0-9a-f]+|0[0-7]*|[1-9][0-9]*)/i.exec(s || ""); if (!m) return 0;
    const v = m[2].length > 1 && m[2][0] === "0" && !/x/i.test(m[2]) ? parseInt(m[2], 8) : Number(m[2]);
    return m[1] === "-" ? -v : v; };
// tests/host.c read_script, the FM-1's lines: [{t, kind, ...}] in time order, and the end
function parse(text) {
    const ev = []; let end = 6500;
    for (const line of text.split("\n")) {
        if (line[0] === "#") continue;
        const w = line.trim().split(/\s+/), t = Number(w[0]), what = w[1];
        if (!/^\d+$/.test(w[0] || "") || !what) continue;
        const a = w.slice(2);
        if (what === "end") end = t;
        else if (what === "keys") ev.push({ t, k: "keys", v: cint(a[0]) >>> 0 });
        else if (what === "btn") ev.push({ t, k: "btn", id: cint(a[0]), how: 2 });
        else if (what === "btndown") ev.push({ t, k: "btn", id: cint(a[0]), how: 1 });
        else if (what === "btnup") ev.push({ t, k: "btn", id: cint(a[0]), how: 0 });
        else if (what === "enc") ev.push({ t, k: "enc", id: cint(a[0]), n: cint(a[1]) });
        else if (what === "midi" || what === "trs") ev.push({ t, k: "midi", m: [cint(a[0]) & 255, cint(a[1]) & 255, cint(a[2]) & 255, what === "trs" ? 2 : 1] });
        else if (what === "clock") { const bpm = cint(a[0]), n = cint(a[1]);
            for (let i = 0; i < n && bpm; i++) ev.push({ t: t + Math.floor(i * 2500 / bpm), k: "midi", m: [0xF8, 0, 0, 1] }); }
        else if (what === "expect" && a.length >= 2) ev.push({ t, k: "expect", v: cint(a[0]), name: a[1].slice(0, 15), args: a.slice(2, 6).map(cint) });
    }
    ev.sort((x, y) => x.t - y.t);            // stable, as host.c's insertion sort
    return { ev, end };
}

async function runEmu(text) {
    const { ev, end } = parse(text), emu = await EmuCore.create(bytes), parts = [];
    let i = 0, fails = 0;
    for (;;) {
        const ms = emu.ms;
        for (; i < ev.length && ev[i].t <= ms; i++) {
            const e = ev[i];
            if (e.k === "keys") emu.keys(e.v);
            else if (e.k === "btn") emu.button(e.id, e.how);
            else if (e.k === "enc") emu.encoder(e.id, e.n);
            else if (e.k === "midi") emu.midi(...e.m);
            else if (e.k === "expect" && emu.probe(e.name, e.args) !== e.v) fails++;
        }
        emu.step();
        const a = emu.takeAudio();
        if (a) parts.push(a);
        if (ms >= end) break;
    }
    const n = parts.reduce((s, p) => s + p.length, 0), out = new Int16Array(n);
    let o = 0;
    for (const p of parts) { out.set(p, o); o += p.length; }
    return { pcm: out, fails };
}
function runHost(name, text) {
    const out = join(tmp, name.replace(/\W/g, "_")), sc = out + ".txt";
    execFileSync("sh", ["-c", 'cat > "$1"', "-", sc], { input: text });
    const r = spawnSync(host, [out, sc], { encoding: "utf8" });
    const wav = readFileSync(out + ".wav");
    return { pcm: new Int16Array(wav.buffer.slice(wav.byteOffset + 44, wav.byteOffset + wav.length)),
             fails: r.status === 0 ? 0 : (r.stdout.match(/FAIL/g) || ["?"]).length };
}

const smk = t => /^\d+\s+(vel|pad|fader|wheel|xkey)\b/m.test(t);
let list = process.argv.slice(2);
if (!list.length) list = ["(tests/host.c's own script)", ...readdirSync("tests/scripts").filter(f => f.endsWith(".txt"))
    .map(f => join("tests/scripts", f)).filter(f => !smk(readFileSync(f, "utf8")))];
let failed = 0, loud = 0;
for (const f of list) {
    const text = f.startsWith("(") ? SCRIPT0 : readFileSync(f, "utf8");
    const t0 = performance.now(), e = await runEmu(text), dt = performance.now() - t0, h = runHost(f, text);
    let diff = -1;
    const n = Math.min(e.pcm.length, h.pcm.length);
    for (let i = 0; i < n; i++) if (e.pcm[i] !== h.pcm[i]) { diff = i >> 1; break; }
    const same = e.pcm.length === h.pcm.length && diff < 0, ok = same && e.fails === h.fails && !e.fails;
    const secs = h.pcm.length / 2 / 44100, peak = h.pcm.reduce((m, v) => Math.max(m, Math.abs(v)), 0);
    if (peak > 1000) loud++;
    console.log(`${f.replace("tests/scripts/", "").padEnd(40)} ${secs.toFixed(1).padStart(5)} s  peak ${String(peak).padStart(5)}  ${(dt / secs / 10).toFixed(1).padStart(4)} % of real time  ` +
        (ok ? "same sound, ok" : `FAILED: ${same ? "same sound" : e.pcm.length !== h.pcm.length ? `frames ${e.pcm.length / 2} vs ${h.pcm.length / 2}` : `differs from frame ${diff}`}` +
         `, expect fails ${e.fails} (host ${h.fails})`));
    if (!ok) failed++;
}
// the one thing host_web.c adds: a button pressed and let go in the same millisecond is a tap (PLAY, after a tap
// that closes the opening), not lost
if (!process.argv[2]) {
    const play = async how => { const e = await EmuCore.create(bytes); let s = 0;
        for (let ms = 1; ms < 3000; ms++) {
            if (e.ms === 300) e.button(12, 2);
            if (e.ms === 500) { if (how) e.button(12, 2); else { e.button(12, 1); e.button(12, 0); } }
            e.step(); const a = e.takeAudio(); if (a) for (const v of a) s = (s * 31 + v) | 0;
        } return s; };
    const tap = await play(1), quick = await play(0), ok = tap === quick && tap !== 0;
    console.log(`${"pressed and let go in one millisecond: a tap".padEnd(86)} ${ok ? "ok" : "FAILED"}`);
    if (!ok) failed++;
}
if (list.length > 10 && loud < list.length / 2) { console.log(`only ${loud} scripts with sound: the comparison proves little`); failed++; }
console.log(failed ? `${failed} FAILED` : "all ok");
process.exit(failed ? 1 : 0);
