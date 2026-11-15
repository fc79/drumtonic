// SPDX-License-Identifier: GPL-3.0-only
// The emulator's audio thread: an AudioWorkletProcessor that runs the firmware (emu-core.js's EmuCore, put before
// this file in the same script: the page loads both as one Blob, so it works from a file on disk) as the sound is
// wanted. The firmware gives 256 frames at a time, the browser takes 128: a FIFO between. The firmware's clock is
// the sound's: 44.1 frames a millisecond, as on the FM-1, so the context must run at 44100 Hz.
//   processorOptions.wasm   the .wasm's bytes (the page decodes them: no atob or fetch needed here)
//   port in:   {t: "keys", mask} {t: "button", id, how} {t: "encoder", id, n} {t: "midi", d: [st, d1, d2], src}
//   port out:  {t: "ready"} {t: "error", msg} and, at most 30 times a second when something changed,
//              {t: "frame", screen: Uint16Array (RGB565, only if it changed), leds: [4 x uint32], ms, saves}
const FIFO = 8192;                               // frames (a power of two)

class DrumtonicEmu extends AudioWorkletProcessor {
    constructor(opt) {
        super();
        this.inq = [];
        this.fifo = new Float32Array(2 * FIFO);
        this.fw = 0; this.fr = 0;
        this.gen = -1; this.leds = ""; this.next = 0;
        this.port.onmessage = e => this.inq.push(e.data);
        EmuCore.create(opt.processorOptions.wasm).then(core => {
            this.core = core;
            this.port.postMessage({ t: "ready" });
        }, err => this.port.postMessage({ t: "error", msg: String(err) }));
    }
    input(m) {
        const c = this.core;
        if (m.t === "keys") c.keys(m.mask);
        else if (m.t === "button") c.button(m.id, m.how);
        else if (m.t === "encoder") c.encoder(m.id, m.n);
        else if (m.t === "midi") c.midi(m.d[0], m.d[1] || 0, m.d[2] || 0, m.src || 1);
    }
    process(inputs, outputs) {
        const out = outputs[0], n = out[0].length;
        if (!this.core || this.dead) return true;   // (silence until the firmware is up)
        try {
            while (this.fw - this.fr < n) {          // the firmware, a millisecond at a time, until there is enough
                for (const m of this.inq.splice(0)) this.input(m);   // (the input lands in the millisecond waiting)
                this.core.step();
                const a = this.core.takeAudio();
                if (a) for (let i = 0; i < a.length; i += 2) {
                    const w = (this.fw++ % FIFO) * 2;
                    this.fifo[w] = a[i] / 32768;
                    this.fifo[w + 1] = a[i + 1] / 32768;
                }
            }
        } catch (err) {
            this.dead = true;
            this.port.postMessage({ t: "error", msg: String(err && err.stack || err) });
            return true;
        }
        const L = out[0], R = out[1] || out[0];
        for (let i = 0; i < n; i++) {
            const r = (this.fr++ % FIFO) * 2;
            L[i] = this.fifo[r];
            R[i] = this.fifo[r + 1];
        }
        if (currentTime >= this.next) {           // the screen and the LEDs, when they changed
            const gen = this.core.screenGen(), leds = this.core.leds(), key = leds.join();
            if (gen !== this.gen || key !== this.leds) {
                const msg = { t: "frame", leds, ms: this.core.ms, saves: this.core.saves() }, tr = [];
                if (gen !== this.gen) { msg.screen = this.core.screen().slice(); tr.push(msg.screen.buffer); }
                this.port.postMessage(msg, tr);
                this.gen = gen; this.leds = key;
                this.next = currentTime + 1 / 30;
            }
        }
        return true;
    }
}
registerProcessor("drumtonic-emu", DrumtonicEmu);
