// SPDX-License-Identifier: GPL-3.0-only
// The emulator on the page's side: starts the sound (an AudioContext at 44100 Hz, the worklet from emu-worklet.js
// with emu-core.js before it, loaded as a Blob so a page opened from disk works), sends the input, draws the screen.
// Everything is passed in: the page (or web/emu/make_page.py) inlines the .wasm as base64 and the worklet's source.
//   const emu = new EmuHost(canvas);  await emu.start(wasmBase64, workletSource);
//   emu.key(k, down)  emu.button(id, how)  emu.encoder(id, n)  emu.midi([st, d1, d2])  emu.volume(0..1)  emu.stop()
//   emu.onleds = leds => ...  (lit and dim, 64-bit masks in 32-bit halves)   emu.onerror = msg => ...
export class EmuHost {
    constructor(canvas) {
        this.canvas = canvas;
        this.g = canvas.getContext("2d");
        this.img = this.g.createImageData(240, 240);
        this.mask = 0;
        this.ms = 0; this.saves = 0;
        this.onleds = null; this.onerror = null; this.onframe = null;
    }
    async start(wasmB64, workletSrc) {
        const ctx = new AudioContext({ sampleRate: 44100, latencyHint: "interactive" });
        this.ctx = ctx;
        if (ctx.sampleRate !== 44100) {               // the firmware's clock is the sound's: it needs 44.1 kHz
            ctx.close();
            throw new Error(`this browser runs its sound at ${ctx.sampleRate} Hz; the emulator needs 44100 Hz`);
        }
        // the worklet from a Blob; a page opened from disk (file://) cannot load one in Chrome: then as a data: URL
        const url = URL.createObjectURL(new Blob([workletSrc], { type: "text/javascript" }));
        try { await ctx.audioWorklet.addModule(url); }
        catch (e) {
            const b = new TextEncoder().encode(workletSrc);
            let s = "";
            for (let i = 0; i < b.length; i += 0x8000) s += String.fromCharCode.apply(null, b.subarray(i, i + 0x8000));
            await ctx.audioWorklet.addModule("data:text/javascript;base64," + btoa(s));
        } finally { URL.revokeObjectURL(url); }
        const bin = atob(wasmB64), bytes = new Uint8Array(bin.length);
        for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
        const node = new AudioWorkletNode(ctx, "drumtonic-emu", { numberOfInputs: 0, numberOfOutputs: 1,
            outputChannelCount: [2], processorOptions: { wasm: bytes.buffer } });
        this.node = node;
        this.gain = ctx.createGain();                 // the MASTER knob (a potentiometer on the FM-1, not the app's)
        node.connect(this.gain).connect(ctx.destination);
        const ready = new Promise((ok, fail) => {
            node.port.onmessage = e => {
                const m = e.data;
                if (m.t === "ready") ok();
                else if (m.t === "error") { fail(new Error(m.msg)); if (this.onerror) this.onerror(m.msg); }
                else if (m.t === "frame") this.frame(m);
            };
        });
        await ctx.resume();
        await ready;
    }
    frame(m) {
        if (m.screen) {
            const s = m.screen, d = this.img.data;
            for (let i = 0, j = 0; i < s.length; i++, j += 4) {   // RGB565 to RGBA, as tests/host.c's PPM
                const c = s[i];
                d[j] = (c >> 11) * 255 / 31; d[j + 1] = (c >> 5 & 63) * 255 / 63; d[j + 2] = (c & 31) * 255 / 31; d[j + 3] = 255;
            }
            this.g.putImageData(this.img, 0, 0);
        }
        this.ms = m.ms; this.saves = m.saves;
        if (this.onleds) this.onleds(m.leds);
        if (this.onframe) this.onframe(m);
    }
    send(m) { if (this.node) this.node.port.postMessage(m); }
    key(k, down) {                                    // note key k: 0 F3 .. 26 G5
        this.mask = down ? this.mask | 1 << k : this.mask & ~(1 << k);
        this.send({ t: "keys", mask: this.mask });
    }
    button(id, how) { this.send({ t: "button", id, how }); }   // 1 pressed, 0 let go, 2 a tap
    encoder(id, n) { this.send({ t: "encoder", id, n }); }
    midi(d, src = 1) { this.send({ t: "midi", d: Array.from(d), src }); }
    volume(v) { if (this.gain) this.gain.gain.value = v; }
    async stop() { if (this.ctx) await this.ctx.close(); this.ctx = this.node = null; }
}
