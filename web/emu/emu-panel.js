// SPDX-License-Identifier: GPL-3.0-only
// The emulator's panel: the FM-1 drawn as on M-VAVE's photo (knobs, screen, 14 buttons, 27 keys with their LEDs), the
// letters of the computer keys on it, a help band that says what the control under the pointer does, a MIDI
// controller if the browser has Web MIDI. It drives an EmuHost (emu-host.js, in the same script before this file).
//   mountEmu(container, { wasm, worklet, onstate })   wasm: the .wasm as base64; worklet: the worklet's source
// returns { pause(), resume() } (the page pauses it when its tab is hidden). The design: the installer's tokens, the
// FM-1's charcoal greys, orange only for the one action and a control kept held (the mockup approved 2026-11-14).

const EMU_CSS = `
.emu { --body: #1b1c1e; --well: #111213; --part: #2b2d30; --partdown: #3a3d41; --hint: #6f747b; display: grid; gap: 12px; }
.emu-stage { position: relative; }
.emu-stage svg { display: block; width: 100%; height: auto; user-select: none; -webkit-user-select: none; touch-action: none; }
.emu-stage canvas { position: absolute; left: 26.8%; top: 7.333%; width: 24%; height: 40%; image-rendering: pixelated; background: #000; }
.emu-stage.off svg, .emu-stage.off canvas { opacity: .35; filter: saturate(.4); }
.emu-cover { position: absolute; inset: 0; display: grid; place-content: center; justify-items: center; padding: 16px; }
.emu-plate { background: var(--bg, #000); border-radius: 3px; padding: 20px 22px; display: grid; justify-items: center; gap: 12px; box-shadow: 0 0 0 1px var(--act, #24262a); text-align: center; }
.emu-plate p { margin: 0; color: var(--txt, #f4f4f2); max-width: 40ch; }
.emu-plate p.emu-err { color: var(--err, #ff2e3e); }
.emu-on { font: 700 26px/1 var(--face, sans-serif); text-transform: uppercase; letter-spacing: .03em; border: 0; border-radius: 3px; padding: 16px 28px 14px; cursor: pointer; background: var(--go, #ff6b4a); color: var(--ink, #000); min-height: 52px; }
.emu-on:disabled { opacity: .5; cursor: progress; }
.emu-on:focus-visible, .emu-midi:focus-visible { outline: 2px solid var(--txt, #f4f4f2); outline-offset: 2px; }
.emu svg .pbody { fill: var(--body); }
.emu svg .well { fill: var(--well); }
.emu svg .bezel { fill: #050505; }
.emu svg .knob { fill: var(--part); }
.emu svg .kring { fill: var(--well); }
.emu svg .kmark { stroke: var(--txt, #f4f4f2); stroke-width: 3; stroke-linecap: round; }
.emu svg .pbtn { fill: var(--part); }
.emu svg .blab { fill: var(--sec, #a4a7ac); font: 700 14px var(--face, sans-serif); text-anchor: middle; letter-spacing: .04em; pointer-events: none; }
.emu svg .btng.glow .blab { fill: #c9ccd0; }
.emu svg .btng.lit .blab { fill: var(--txt, #f4f4f2); }
.emu svg .btng.lit .pbtn { fill: #3d3f43; }
.emu svg .silk { fill: var(--sec, #a4a7ac); font: 500 15px var(--face, sans-serif); text-anchor: middle; letter-spacing: .08em; }
.emu svg .silk.small { font-size: 12px; fill: var(--hint); pointer-events: none; }
.emu svg .key { fill: var(--part); }
.emu svg .led { fill: #3d4044; pointer-events: none; }
.emu svg .led.glow { fill: #75787d; }
.emu svg .led.on { fill: var(--txt, #f4f4f2); }
.emu svg .ctl { cursor: pointer; outline: none; }
.emu svg .ctl.down .key, .emu svg .ctl.down .pbtn, .emu svg .ctl.down .knob { fill: var(--partdown); }
.emu svg .ctl.held .key, .emu svg .ctl.held .pbtn, .emu svg .ctl.held .kring { stroke: var(--go, #ff6b4a); stroke-width: 3; }
.emu svg .ctl:focus-visible .key, .emu svg .ctl:focus-visible .pbtn, .emu svg .ctl:focus-visible .kring { stroke: var(--txt, #f4f4f2); stroke-width: 2; stroke-dasharray: 4 3; }
.emu svg .cap { pointer-events: none; }
.emu svg .cap rect { fill: none; stroke: var(--hint); stroke-width: 1.2; }
.emu svg .cap text { fill: var(--hint); font: 700 12px var(--face, sans-serif); text-anchor: middle; }
.emu svg .cap.mini rect { fill: var(--well); }
.emu svg .cap.mini text { font-size: 11px; }
.emu svg .cap.hit rect { stroke: var(--txt, #f4f4f2); }
.emu svg .cap.hit text { fill: var(--txt, #f4f4f2); }
.emu-help { background: var(--box, #17181a); border-radius: 3px; padding: 10px 12px 8px; display: grid; grid-template-columns: minmax(0, 1fr) auto; gap: 4px 16px; align-items: baseline; min-height: 64px; }
.emu-help b { font: 700 22px/1.1 var(--face, sans-serif); text-transform: uppercase; letter-spacing: .03em; }
.emu-help .what { color: var(--sec, #a4a7ac); grid-column: 1 / -1; }
.emu-help.err .what { color: var(--err, #ff2e3e); }
.emu kbd { font: 700 15px var(--face, sans-serif); border: 1px solid var(--hint); color: var(--txt, #f4f4f2); border-radius: 3px; padding: 0 6px; }
.emu-tips { display: flex; flex-wrap: wrap; gap: 6px 18px; color: var(--sec, #a4a7ac); font-size: 16px; align-items: center; }
.emu-midi { font: 700 15px/1 var(--face, sans-serif); text-transform: uppercase; letter-spacing: .03em; border: 0; border-radius: 3px; padding: 8px 12px 6px; cursor: pointer; background: var(--act, #24262a); color: var(--txt, #f4f4f2); }
`;

const EMU_WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26];   // note keys (0 F3 .. 26 G5)
const EMU_BLACK = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25];
const EMU_BTN = ["OCT-", "OCT+", "FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"];   // BTN_*
const EMU_CH = ["BD", "SD", "CP", "CH", "OH", "LT", "MT", "HT", "RS", "CY", "11"];
// the computer's keys, by place (KeyboardEvent.code: the same place on any layout), and the letter printed by default
const EMU_KEYS = [
    ..."1234567890".split("").map((c, i) => ["Digit" + c, "kb-" + i, c]), ["Minus", "kb-10", "-"],
    ..."QWERTYUIASDFGHJK".split("").map((c, i) => ["Key" + c, "kw-" + i, c]),
    ["KeyZ", "b-HOME", "Z"], ["KeyX", "b-SEQ", "X"], ["KeyC", "b-EDIT", "C"], ["KeyV", "b-ENV", "V"], ["KeyB", "b-LFO", "B"],
    ["KeyN", "b-FX", "N"], ["KeyM", "b-ARP", "M"], ["Comma", "b-SEL", ","], ["Period", "b-GLO", "."], ["Slash", "b-SAVE", "/"],
    ["Space", "b-PLAY", "␣"], ["Enter", "b-REC", "↵"], ["NumpadEnter", "b-REC", null], ["ArrowLeft", "b-OCT-", "←"],
    ["ArrowRight", "b-OCT+", "→"]];
const EMU_HELP = {
    MASTER: "Volume.",
    SELECT: "Picks the channel: 1–10, then ALL. In a list, the row.",
    PRESETS: "On STEPS: the channel's length, 1 to 64 steps.",
    ALGORITHM: "On STEPS: shifts the channel's steps. With a step held: its condition.",
    KNOB: "The value above it on the screen. Hold a step and turn: a p-lock on that step.",
    "OCT-": "LIVE: patterns 1–16. STEPS: the 16 steps before. With HOME held: undo.",
    "OCT+": "LIVE: patterns 17–32. STEPS: the 16 steps after. With HOME held: redo.",
    FX: "FX: reverb, delay, master. Pressed again, the next bank.",
    SEL: "Held: FILL. In a list: does the row.",
    ENV: "MIX: every channel's level and sends; the 8 kits on the white keys.",
    LFO: "The pattern's 8 LFOs. Hold LFO and turn a knob to give it one.",
    EDIT: "SOUND: the channel's sound in banks; the last bank is its library.",
    GLO: "Held with a knob: takes away its motion and LFO.",
    HOME: "LIVE: tempo, swing, the patterns on the white keys. Pressed again: SETTINGS. Held: the guide.",
    SAVE: "Saves everything, even while playing (here, until the page closes).",
    ARP: "LIVE: beat repeat. STEPS: note repeat. With a step held: a ratchet.",
    SEQ: "STEPS: the grid of 10 channels by 16 steps.",
    PLAY: "Starts and stops.",
    REC: "Records the keys you play and the knobs you turn.",
};

function emuPanelSvg() {
    const P = [], esc = s => String(s).replace(/&/g, "&amp;").replace(/"/g, "&quot;").replace(/</g, "&lt;");
    const cap = (x, y, t, w = 18, h = 15, cls = "cap") =>
        `<g class="${cls}" aria-hidden="true"><rect x="${x - w / 2}" y="${y}" width="${w}" height="${h}" rx="3"/><text x="${x}" y="${y + h - 3}">${esc(t)}</text></g>`;
    const knob = (id, x, y, label, help, key) => {
        const r = 22;
        P.push(`<text class="silk" x="${x}" y="${y - r - 16}" aria-hidden="true">${label}</text>`,
            `<g class="ctl knobg" id="${id}" data-name="${label}" data-help="${esc(help)}" data-key="${key || ""}" tabindex="0" ` +
            (id === "k-MASTER" ? `role="slider" aria-valuemin="0" aria-valuemax="100" aria-valuenow="80"` : `role="spinbutton" aria-valuenow="0"`) +
            ` aria-label="${label}"><circle class="kring" cx="${x}" cy="${y}" r="${r + 7}"/><circle class="knob" cx="${x}" cy="${y}" r="${r}"/>` +
            `<g class="kmarkg" style="transform-origin:${x}px ${y}px"><line class="kmark" x1="${x}" y1="${y - r + 4}" x2="${x}" y2="${y - r + 13}"/></g></g>`);
        if (key) P.push(cap(x, y + r + 12, key, 32));
    };
    const btn = (n, x, y, w, h, ly) => {
        const name = n === "PLAY" ? "PLAY/STOP" : n;
        P.push(`<g class="ctl btng" id="b-${n}" data-name="${name}" data-help="${esc(EMU_HELP[n])}" tabindex="0" role="button" aria-label="${name}">` +
            `<rect class="pbtn" x="${x - w / 2}" y="${y}" width="${w}" height="${h}" rx="8"/>` +
            (n === "PLAY" ? `<text class="blab" x="${x}" y="${y + 21}">PLAY</text><text class="blab" x="${x}" y="${y + 36}">STOP</text>`
                          : `<text class="blab" x="${x}" y="${ly}">${n}</text>`) + `</g>`);
    };
    P.push(`<rect class="pbody" x="0" y="0" width="1000" height="600" rx="44"/>`);
    knob("k-MASTER", 76, 90, "MASTER", EMU_HELP.MASTER);
    knob("k-SELECT", 184, 90, "SELECT", EMU_HELP.SELECT, "↑ ↓");
    knob("k-PRESETS", 76, 200, "PRESETS", EMU_HELP.PRESETS);
    knob("k-ALGORITHM", 184, 200, "ALGORITHM", EMU_HELP.ALGORITHM);
    P.push(`<rect class="well" x="48" y="262" width="166" height="54" rx="12"/>`);
    for (const [x, n] of [[90, "OCT-"], [172, "OCT+"]]) { btn(n, x, 272, 64, 34, 295); P.push(cap(x, 322, n === "OCT-" ? "←" : "→", 22, 17)); }
    P.push(`<rect class="bezel" x="254" y="40" width="268" height="248" rx="22"/>`);
    [564, 678, 792, 906].forEach((x, i) => knob("k-KNOB" + (i + 1), x, 92, "KNOB" + (i + 1), EMU_HELP.KNOB));
    P.push(`<rect class="well" x="536" y="170" width="414" height="128" rx="14"/>`);
    [["FX", "SEL", "ENV", "LFO", "EDIT", "GLO"], ["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"]].forEach((row, r) => row.forEach((n, c) => {
        const x = [576, 642, 706, 772, 836, 896][c], y = [200, 266][r];
        btn(n, x, y - 24, 54, 46, y + 5);
        P.push(cap(x + 22, y - 32, "", 18, 16, "cap mini"));
    }));
    P.push(`<rect class="well" x="30" y="350" width="940" height="222" rx="26"/>`);
    [100, 156, 214, 332, 388, 500, 556, 614, 728, 784, 900].forEach((x, i) => {
        const name = i === 10 ? "Black key 11" : EMU_CH[i];
        const help = i === 10 ? "Held with white keys: the channel's accents." : `${EMU_CH[i]}: on LIVE mutes it; on the other pages picks it.`;
        P.push(`<g class="ctl keyg" id="kb-${i}" data-name="${name}" data-help="${help}" tabindex="0" role="button" aria-label="${i === 10 ? name : "Channel " + name}">` +
            `<rect class="key" x="${x - 20}" y="362" width="40" height="80" rx="20"/><rect class="led" x="${x - 2.5}" y="372" width="5" height="30" rx="2.5"/>` +
            `<text class="silk small" x="${x}" y="418">${EMU_CH[i]}</text></g>`, cap(x, 423, ""));
    });
    for (let i = 0; i < 16; i++) {
        const x = 70 + 57.6 * i;
        P.push(`<g class="ctl keyg" id="kw-${i}" data-name="White key ${i + 1}" data-help="STEPS: step ${i + 1}. LIVE: pattern ${i + 1}.${i < 8 ? ` MIX: kit ${i + 1}.` : ""}" tabindex="0" role="button" aria-label="White key ${i + 1}">` +
            `<rect class="key" x="${x - 23}" y="452" width="46" height="100" rx="20"/><rect class="led" x="${x - 2.5}" y="466" width="5" height="52" rx="2.5"/></g>`, cap(x, 527, ""));
    }
    return `<svg viewBox="0 0 1000 600" role="group" aria-label="The FM-1 panel">${P.join("")}</svg>`;
}

function mountEmu(root, opt) {
    if (!document.getElementById("emu-css")) {
        const st = document.createElement("style"); st.id = "emu-css"; st.textContent = EMU_CSS; document.head.append(st);
    }
    root.classList.add("emu");
    root.innerHTML = `<div class="emu-stage off">${emuPanelSvg()}<canvas width="240" height="240" role="img" aria-label="The FM-1 screen"></canvas>
      <div class="emu-cover"><div class="emu-plate"><button class="emu-on" type="button">Switch on</button>
      <p>The real Drumtonic runs here, the same firmware the Install tab puts on an FM-1. The sound starts with the click.</p></div></div></div>
      <div class="emu-help" aria-live="polite"><b>How to play</b><span class="key"></span><span class="what">Point at any control to see what it does. The grey letters are your computer's keys.</span></div>
      <div class="emu-tips"><span><kbd>1</kbd>–<kbd>0</kbd> the 10 channels</span><span><kbd>Q</kbd>–<kbd>I</kbd> <kbd>A</kbd>–<kbd>K</kbd> the 16 white keys</span>
      <span><kbd>Space</kbd> play · <kbd>Enter</kbd> rec</span><span>Knobs: the mouse wheel, or drag up and down</span>
      <span>Hold a key and turn a knob: a p-lock</span><span><kbd>Shift</kbd> + click keeps a control held</span>
      <span class="emu-midibox"></span></div>`;
    const q = s => root.querySelector(s), svg = q("svg"), stage = q(".emu-stage"), canvas = q("canvas");
    const emu = new EmuHost(canvas);
    const ctl = id => svg.querySelector("#" + CSS.escape(id));
    const byCode = {}, capOf = {};
    for (const [code, id, label] of EMU_KEYS) {
        byCode[code] = id;
        if (label === null) continue;
        const g = ctl(id);
        let c = g && g.nextElementSibling;        // (each control's cap follows it; OCT's and SELECT's are fixed)
        if (id.startsWith("b-OCT")) c = null;
        if (c && c.classList.contains("cap")) { c.querySelector("text").textContent = label; capOf[id] = c; g.dataset.key = label === "␣" ? "Space" : label === "↵" ? "Enter" : label; }
    }
    capOf["b-OCT-"] = ctl("b-OCT-").nextElementSibling; ctl("b-OCT-").dataset.key = "←";
    capOf["b-OCT+"] = ctl("b-OCT+").nextElementSibling; ctl("b-OCT+").dataset.key = "→";
    if (navigator.keyboard && navigator.keyboard.getLayoutMap)   // this keyboard's own letters, where the browser says them
        navigator.keyboard.getLayoutMap().then(m => {
            for (const [code, id] of EMU_KEYS) {
                const ch = m.get(code), c = capOf[id];
                if (!ch || !c || !/^(Digit|Key|Minus|Comma|Period|Slash)/.test(code)) continue;
                c.querySelector("text").textContent = ch.toUpperCase(); ctl(id).dataset.key = ch.toUpperCase();
            }
        }, () => {});

    // ---- help band
    const help = q(".emu-help"), hName = q(".emu-help b"), hKey = q(".emu-help .key"), hWhat = q(".emu-help .what");
    function say(name, key, what, err) {
        hName.textContent = name; hKey.innerHTML = ""; hWhat.textContent = what; help.classList.toggle("err", !!err);
        if (key) { const k = document.createElement("kbd"); k.textContent = key; hKey.append(k); }
    }
    const explain = g => say(g.dataset.name, g.dataset.key || (g.classList.contains("knobg") ? "wheel or drag" : ""),
        g.dataset.help + (held.has(g.id) ? " Kept held: click it again to let go." : ""));

    // ---- pressing: a control is down while any of its sources (the mouse, a key, kept held) holds it
    let on = false;
    const src = {}, held = new Set();
    function send(id, down) {
        if (id.startsWith("b-")) emu.button(EMU_BTN.indexOf(id.slice(2)), down ? 1 : 0);
        else emu.key(id[1] === "w" ? EMU_WHITE[+id.slice(3)] : EMU_BLACK[+id.slice(3)], down);
    }
    function press(id, from) {
        const s = src[id] || (src[id] = new Set());
        if (!on || s.has(from)) return;
        s.add(from);
        if (s.size > 1) return;
        ctl(id).classList.add("down"); if (capOf[id]) capOf[id].classList.add("hit");
        send(id, true);
    }
    function release(id, from) {
        const s = src[id];
        if (!s || !s.delete(from) || s.size) return;
        ctl(id).classList.remove("down"); if (capOf[id]) capOf[id].classList.remove("hit");
        send(id, false);
    }
    function releaseAll(from) { for (const id in src) release(id, from); }
    svg.querySelectorAll(".ctl:not(.knobg)").forEach(g => {
        g.addEventListener("pointerenter", () => explain(g));
        g.addEventListener("focus", () => explain(g));
        g.addEventListener("pointerdown", e => {
            e.preventDefault();
            if (!on || e.button) return;              // (no focus: Space and Enter stay PLAY and REC)
            if (held.has(g.id)) { held.delete(g.id); g.classList.remove("held"); release(g.id, "held"); explain(g); return; }
            if (e.shiftKey) { held.add(g.id); g.classList.add("held"); press(g.id, "held"); explain(g); return; }
            press(g.id, "mouse");
            const up = () => { release(g.id, "mouse"); removeEventListener("pointerup", up); removeEventListener("pointercancel", up); };
            addEventListener("pointerup", up); addEventListener("pointercancel", up);
        });
        g.addEventListener("keydown", e => {
            if (e.key !== "Enter" && e.key !== " ") return;
            e.preventDefault(); e.stopPropagation();
            if (!e.repeat) press(g.id, "focus");
        });
        g.addEventListener("keyup", e => { if (e.key === "Enter" || e.key === " ") { e.stopPropagation(); release(g.id, "focus"); } });
    });

    // ---- knobs: encoders turn in detents (one a wheel notch, or 8 px of drag); MASTER is the volume
    const ENC = { SELECT: 0, ALGORITHM: 1, KNOB1: 2, KNOB2: 3, KNOB3: 4, KNOB4: 5, PRESETS: 6 };
    const pos = {};
    let vol = 80;
    function turn(g, d) {
        if (!on || !d) return;
        const n = g.id.slice(2);
        let deg;
        if (n === "MASTER") {
            vol = Math.max(0, Math.min(100, vol + d * 4)); emu.volume((vol / 100) ** 2);
            g.setAttribute("aria-valuenow", vol); deg = (vol - 50) * 2.7;
        } else {
            emu.encoder(ENC[n], d);
            pos[n] = (pos[n] || 0) + d; g.setAttribute("aria-valuenow", pos[n]); deg = pos[n] * 15;
        }
        g.querySelector(".kmarkg").style.transform = `rotate(${deg}deg)`;
        explain(g);
    }
    svg.querySelectorAll(".knobg").forEach(g => {
        let acc = 0;
        g.addEventListener("pointerenter", () => explain(g));
        g.addEventListener("focus", () => explain(g));
        g.addEventListener("wheel", e => {
            e.preventDefault();
            acc -= e.deltaMode ? e.deltaY * 40 : e.deltaY;   // (lines to pixels)
            const d = Math.trunc(acc / 60) || (Math.abs(acc) >= 30 && Math.abs(e.deltaY) >= 30 ? Math.sign(acc) : 0);
            if (d) { acc -= d * 60; if (Math.abs(acc) < 60) acc = 0; turn(g, d); }
        }, { passive: false });
        g.addEventListener("pointerdown", e => {
            e.preventDefault();
            if (!on) return;
            let y = e.clientY; g.classList.add("down");
            const mv = m => { const d = Math.trunc((y - m.clientY) / 8); if (d) { turn(g, d); y -= d * 8; } };
            const up = () => { g.classList.remove("down"); removeEventListener("pointermove", mv); removeEventListener("pointerup", up); };
            addEventListener("pointermove", mv); addEventListener("pointerup", up);
        });
        g.addEventListener("keydown", e => {
            const d = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1 }[e.key];
            if (d) { e.preventDefault(); e.stopPropagation(); turn(g, d); }
        });
    });

    // ---- the computer keyboard (once on, while the panel is shown, never with Cmd / Ctrl / Alt)
    const shown = () => on && root.offsetParent !== null;
    addEventListener("keydown", e => {
        if (!shown() || e.metaKey || e.ctrlKey || e.altKey) return;
        if (e.target.closest && e.target.closest('[role="tablist"], input, textarea, select')) return;
        if (e.code === "ArrowUp" || e.code === "ArrowDown") { e.preventDefault(); turn(ctl("k-SELECT"), e.code === "ArrowUp" ? -1 : 1); return; }
        const id = byCode[e.code];
        if (!id) return;
        e.preventDefault();
        if (!e.repeat) press(id, "key:" + e.code);
    });
    addEventListener("keyup", e => { const id = byCode[e.code]; if (id) release(id, "key:" + e.code); });
    addEventListener("blur", () => { for (const id in src) for (const f of [...src[id]]) if (f !== "held") release(id, f); });

    // ---- LEDs: the firmware's, lit and dim (ids 0..13 buttons, 14 + key the keys)
    const ledEl = [];
    EMU_BTN.forEach((n, i) => ledEl[i] = [ctl("b-" + n), "lit", "glow"]);
    EMU_WHITE.forEach((k, i) => ledEl[14 + k] = [ctl("kw-" + i).querySelector(".led"), "on", "glow"]);
    EMU_BLACK.forEach((k, i) => ledEl[14 + k] = [ctl("kb-" + i).querySelector(".led"), "on", "glow"]);
    emu.onleds = l => ledEl.forEach(([el, a, b], id) => {
        const w = id >> 5, bit = 1 << (id & 31), lit = !!(l[w] & bit), dim = !lit && !!(l[2 + w] & bit);
        el.classList.toggle(a, lit); el.classList.toggle(b, dim);
    });
    emu.onerror = m => say("Stopped", "", "The emulator stopped: " + m + " Reload the page to start again.", true);

    // ---- a MIDI controller: notes, CCs and the clock to the FM-1's USB MIDI in
    const midibox = q(".emu-midibox");
    function midiUi(text, button) {
        midibox.innerHTML = "";
        if (button) { const b = document.createElement("button"); b.className = "emu-midi"; b.type = "button"; b.textContent = button; b.onclick = midiOn; midibox.append(b); }
        if (text) midibox.append((button ? " " : "") + text);
    }
    let midi = null;
    function midiPorts() {
        const names = [];
        for (const i of midi.inputs.values()) {
            if (i.state !== "connected") continue;
            names.push(i.name);
            i.onmidimessage = e => {
                const d = e.data, s = d[0];
                if (s >= 0x80 && s < 0xF0) emu.midi(d.slice(0, 3));
                else if (s === 0xF8 || s === 0xFA || s === 0xFB || s === 0xFC) emu.midi([s]);
            };
        }
        midiUi(names.length ? "MIDI in: " + names.join(", ") : "MIDI: no controller connected", "");
    }
    async function midiOn() {
        try { midi = await navigator.requestMIDIAccess({ sysex: false }); midi.onstatechange = midiPorts; midiPorts(); }
        catch (e) { midiUi("MIDI was not allowed", "Use a MIDI controller"); }
    }
    if (navigator.requestMIDIAccess) midiUi("", "Use a MIDI controller");
    else midiUi("A MIDI controller needs Chrome or Edge", "");

    // ---- switching on
    const onBtn = q(".emu-on"), plateP = q(".emu-plate p");
    onBtn.onclick = async () => {
        onBtn.disabled = true; onBtn.textContent = "Starting…";
        try {
            await emu.start(opt.wasm, opt.worklet);
            emu.volume((vol / 100) ** 2);
            on = true;
            q(".emu-cover").style.display = "none"; stage.classList.remove("off");
            say("Switched on", "Space", "Press any key or button to close the opening screen, then PLAY (the space bar).");
            if (document.activeElement) document.activeElement.blur();   // (the keys go to the panel)
            if (opt.onstate) opt.onstate("on");
        } catch (e) {
            onBtn.disabled = false; onBtn.textContent = "Switch on";
            plateP.className = "emu-err";
            plateP.textContent = "This browser could not start it: " + (e.message || e) + ". Try a current Chrome, Edge, Firefox or Safari.";
        }
    };
    return {
        pause() { if (emu.ctx && emu.ctx.state === "running") emu.ctx.suspend(); for (const id in src) for (const f of [...src[id]]) if (f !== "held") release(id, f); },
        resume() { if (emu.ctx && emu.ctx.state === "suspended") emu.ctx.resume(); },
        host: emu,
    };
}
