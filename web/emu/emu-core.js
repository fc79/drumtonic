// SPDX-License-Identifier: GPL-3.0-only
// The emulator's core: Drumtonic's firmware as WebAssembly (build/emu/drumtonic-emu.wasm, web/emu/build.sh), run
// a millisecond at a time. No browser API: the same code runs in the page's audio thread (an AudioWorklet) and in
// node (web/emu/test_emu.mjs). The app never returns (its loop calls platform_poll every millisecond); host_web.c's
// platform_poll calls emu_yield, where Asyncify unwinds the app back to here, and step() rewinds it into the same
// place. Between two steps the app is suspended: the inputs (keys, buttons, encoders, MIDI) land in that
// millisecond, as tests/host.c's script does.

export const BTN = { "OCT-": 0, "OCT+": 1, FX: 2, SEL: 3, ENV: 4, LFO: 5, EDIT: 6, GLO: 7, HOME: 8, SAVE: 9, ARP: 10,
                     SEQ: 11, PLAY: 12, REC: 13 };                       // platform.c BTN_* (the FM-1)
export const ENC = { SELECT: 0, ALGORITHM: 1, KNOB1: 2, KNOB2: 3, KNOB3: 4, KNOB4: 5, PRESETS: 6 };   // ENC_*

export class EmuCore {
    // bytes: the .wasm (ArrayBuffer or typed array); or a compiled WebAssembly.Module
    static async create(bytes) {
        const core = new EmuCore();
        const mod = bytes instanceof WebAssembly.Module ? bytes : await WebAssembly.compile(bytes);
        core.inst = await WebAssembly.instantiate(mod, { env: { emu_yield: () => core.yielded() } });
        core.x = core.inst.exports;
        core.x._initialize();
        core.data = core.x.emu_asyncify_data();
        core.state = 0;                          // 0 running, 1 unwinding (suspending), 2 rewinding (resuming)
        core.started = false;
        core.ring = core.x.emu_audio_ring();
        core.read = 0;                           // the frames taken so far (emu_audio_w: written)
        core.step();                             // the app's start-up, up to its first millisecond
        return core;
    }
    yielded() {
        if (this.state === 2) {                  // back where it stopped: on with the app
            this.x.asyncify_stop_rewind();
            this.state = 0;
        } else {                                 // the app waits for the page: unwind to step()
            this.state = 1;
            this.x.asyncify_start_unwind(this.data);
        }
    }
    // one turn of the app: from where it waits, through this millisecond's sound and its loop, to the next
    step() {
        if (this.started) {
            this.state = 2;
            this.x.asyncify_start_rewind(this.data);
        }
        this.started = true;
        this.x.emu_boot();
        if (this.state !== 1) throw new Error("emu: the app returned");
        this.x.asyncify_stop_unwind();
        this.state = 0;
    }
    get ms() { return this.x.emu_ms(); }

    // ---- input (it lands in the millisecond now waiting)
    keys(mask) { this.x.emu_keys(mask >>> 0); }                  // the note keys held: bit 0 F3 .. 26 G5
    button(id, how) { this.x.emu_button(id, how); }               // 1 pressed, 0 let go, 2 a tap
    encoder(id, detents) { this.x.emu_encoder(id, detents | 0); } // + clockwise
    midi(st, d1 = 0, d2 = 0, src = 1) { this.x.emu_midi(st, d1, d2, src); }   // src 1 USB, 2 TRS

    // ---- output
    // the sound written since the last call, as int16 stereo frames (L R L R); null if none
    takeAudio() {
        const w = this.x.emu_audio_w() >>> 0, n = (w - this.read) >>> 0;
        if (!n) return null;
        if (n > this.ring) throw new Error("emu: the sound ring overran");
        const ring = new Int16Array(this.x.memory.buffer, this.x.emu_audio_buf(), 2 * this.ring);
        const out = new Int16Array(2 * n);
        for (let i = 0; i < n; i++) {
            const r = (this.read + i) % this.ring;
            out[2 * i] = ring[2 * r];
            out[2 * i + 1] = ring[2 * r + 1];
        }
        this.read = w;
        return out;
    }
    screenGen() { return this.x.emu_screen_gen() >>> 0; }        // changes when the screen does
    screen() { return new Uint16Array(this.x.memory.buffer, this.x.emu_screen(), 240 * 240); }   // RGB565 (a view)
    // LEDs: lit and dim, as two 64-bit masks split in 32-bit halves (bit id: 0..13 buttons, 14 + key the keys)
    leds() { return [0, 1, 2, 3].map(i => this.x.emu_leds(i) >>> 0); }
    saves() { return this.x.emu_saved() >>> 0; }
    // tests/host.c's "expect": the app's state NAME with its arguments; null if the app has no such name
    probe(name, args = []) {
        const nm = new Uint8Array(this.x.memory.buffer, this.x.emu_probe_name(), 16);
        nm.fill(0);
        for (let i = 0; i < Math.min(name.length, 15); i++) nm[i] = name.charCodeAt(i) & 127;   // (no TextEncoder in a worklet)
        new Int32Array(this.x.memory.buffer, this.x.emu_probe_args(), 4).set(args.slice(0, 4).concat([0, 0, 0, 0]).slice(0, 4));
        const v = this.x.emu_probe(args.length);
        return v === -32768 ? null : v;
    }
    list(spec) {                                // tests/host.c's "list" (the tests): 0 done, -1 not a list
        const b = new Uint8Array(this.x.memory.buffer, this.x.emu_list_spec(), 256);
        b.fill(0);
        for (let i = 0; i < Math.min(spec.length, 255); i++) b[i] = spec.charCodeAt(i) & 127;
        return this.x.emu_list();
    }
}
