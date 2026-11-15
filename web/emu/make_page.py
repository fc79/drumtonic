#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fill an emulator page: the .wasm as base64, the worklet's source (emu-core.js then emu-worklet.js, as one Blob),
the page's side (emu-host.js, then the panel: emu-panel.js), all inlined: one HTML file that works from disk and
offline.
  web/emu/make_page.py [--version V] [--wasm build/emu/drumtonic-emu.wasm] [--page web/emu/dev.html] [--out F]
The placeholders: @EMU_WASM@ (in a JS string), @EMU_WORKLET@ (a JS string), /*@EMU_CODE@*/ (code, in a module script:
EmuHost and mountEmu); the dev page's @VERSION@ is filled here. tools/make_installer.py uses fill() for its Try tab."""
import argparse
import base64
import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent


def _js(name):
    """a module of this folder as plain code (its exports become declarations)"""
    return re.sub(r"^export ", "", (HERE / name).read_text(), flags=re.M)


def fill(page, wasm):
    worklet = _js("emu-core.js") + "\n" + (HERE / "emu-worklet.js").read_text()
    sub = {"@EMU_WASM@": base64.b64encode(Path(wasm).read_bytes()).decode(),
           "@EMU_WORKLET@": json.dumps(worklet).replace("</", "<\\/"),   # (a string in a <script>: no "</script>")
           "/*@EMU_CODE@*/": (_js("emu-host.js") + "\n" + _js("emu-panel.js")).replace("</script", "<\\/script")}
    for k, v in sub.items():
        if k not in page:
            raise SystemExit(f"make_page: {k} is not in the page")
        page = page.replace(k, v)
    return page


def main():
    a = argparse.ArgumentParser()
    a.add_argument("--version", default="dev")
    a.add_argument("--wasm", default=str(ROOT / "build/emu/drumtonic-emu.wasm"))
    a.add_argument("--page", default=str(HERE / "dev.html"))
    a.add_argument("--out", default=str(ROOT / "build/emu/drumtonic-emu-dev.html"))
    a = a.parse_args()
    out = fill(Path(a.page).read_text().replace("@VERSION@", a.version), a.wasm)
    Path(a.out).write_text(out)
    print(f"page   {a.out}  {len(out) // 1024} KB")


if __name__ == "__main__":
    main()
