# The web emulator

The installer page's **Try** tab: Drumtonic's FM-1 firmware as WebAssembly, on a drawn FM-1, in any browser that has
an AudioWorklet. Nothing is installed and nothing leaves the page.

| File | What it is |
| --- | --- |
| `host_web.c` | The fake FM-1 the firmware runs on: `tests/host.c` without its script, files and `main()`; the page calls its `emu_*` exports. |
| `build.sh` | `firmware/src/dinghy.c` plus `host_web.c` through Emscripten (Docker), then Asyncify: `build/emu/drumtonic-emu.wasm`. |
| `emu-core.js` | Runs the .wasm: a firmware millisecond a step, the input, the sound, the screen and the lights. No browser API. |
| `emu-worklet.js` | The AudioWorklet: steps the firmware as the sound card asks for samples, so the firmware's clock is the sound's. |
| `emu-host.js` | The page's side: the AudioContext at 44100 Hz, the worklet, the screen on a canvas. |
| `emu-panel.js` | The panel: the FM-1 in SVG, the mouse, the keyboard, Web MIDI, the help. |
| `make_page.py` | Inlines all of it (the .wasm as base64) into a page: one HTML file that works from disk. |
| `dev.html` | The emulator alone, for development (`tools/emu.sh`). |
| `test_emu.mjs` | The sound of every FM-1 script in `tests/scripts`, compared bit for bit with `tests/run_host.sh`'s. |

```sh
tools/emu.sh                                   # this checkout's firmware: build/emu/drumtonic-emu-dev.html, opened
web/emu/build.sh 0.2.4 && node web/emu/test_emu.mjs
python3 tools/make_installer.py build/drumtonic-0.2.4.fwsc -o drumtonic-0.2.4-installer.html   # the Try tab too
```

`EMU_DOCKER=0 web/emu/build.sh` uses an emcc on PATH instead of Docker (emsdk 4.0.10, as the image). A new platform
call in `tests/host.c` needs the same in `host_web.c`.
