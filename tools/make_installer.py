#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the Drumtonic web installer: one HTML file with the FM-1 package inside.

  tools/make_installer.py [PACKAGE] [-o OUT]       (defaults: build/drumtonic.fwsc, build/drumtonic-installer.html)
  tools/make_installer.py build/drumtonic-0.1.fwsc -o build/drumtonic-0.1-installer.html   (a release: "0.1 (commit)")

web/installer.html is the page; web/fm1pkg.js and web/fm1ota.js (the same files web/test_web.mjs tests) are inlined
into it, their `export` words dropped. The package is checked first with tools/fm1_install.py's own checks
(identity FM-1_923, the Felucca loader marker, the files' CRCs): a bad package makes no installer. The version shown
is the last commit that touched firmware/ (with "+changes" when firmware/ has uncommitted edits), and the page
prints the package's sha256 so a copy can be matched to its build. Open the result in Chrome or Edge: it needs Web
MIDI with SysEx, nothing else, no network but the font."""
import argparse
import base64
import hashlib
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fm1_install as I  # noqa: E402

PRODUCT = "FM-1_923"


def git(*args):
    try:
        return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def version(package):
    """the commit, and a release's number first when the package is one (build/drumtonic-0.1.fwsc)"""
    v = git("log", "-1", "--format=%h", "--", "firmware/") or "dev"
    v += "+changes" if git("status", "--porcelain", "--", "firmware/") else ""
    m = re.fullmatch(r"drumtonic-(\d\.\d(?:\.\d)?(?:-[A-Za-z0-9]+)?)\.fwsc", Path(package).name)
    return f"{m[1]} ({v})" if m else v


def module(path):
    """a web/ module as plain script text: no import lines, no export words"""
    src = path.read_text()
    src = re.sub(r"^import .*$", "", src, flags=re.M)
    return re.sub(r"^export (?=(const|function|class|async)\b)", "", src, flags=re.M)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("package", nargs="?", default=str(ROOT / "build/drumtonic.fwsc"))
    ap.add_argument("-o", "--out", default=str(ROOT / "build/drumtonic-installer.html"))
    a = ap.parse_args(argv)
    try:
        product, _ = I.load_package(a.package, False)
    except I.InstallError as e:
        print(f"make_installer: {e}", file=sys.stderr)
        return 2
    if product != PRODUCT:
        print(f"make_installer: {a.package} is {product}, not {PRODUCT}", file=sys.stderr)
        return 2
    built = Path(a.package).stat().st_mtime
    newer = [p for p in (ROOT / "firmware").rglob("*") if p.is_file() and p.stat().st_mtime > built]
    if newer:
        print(f"make_installer: {a.package} is older than firmware/ ({newer[0].relative_to(ROOT)}...): build it "
              "first (./build.sh --board fm1), so the version on the page is the one inside", file=sys.stderr)
        return 2
    raw = Path(a.package).read_bytes()
    page = (ROOT / "web/installer.html").read_text()
    fill = {"/*@FM1PKG@*/": module(ROOT / "web/fm1pkg.js"), "/*@FM1OTA@*/": module(ROOT / "web/fm1ota.js"),
            "@PKG_B64@": base64.b64encode(raw).decode(), "@VERSION@": version(a.package), "@PRODUCT@": PRODUCT,
            "@SIZE@": str(round(len(raw) / 1024)), "@SHA@": hashlib.sha256(raw).hexdigest()[:12]}
    for k, v in fill.items():
        if k not in page:
            print(f"make_installer: web/installer.html has no {k}", file=sys.stderr)
            return 2
        page = page.replace(k, v)
    Path(a.out).write_text(page)
    print(f"installer {a.out}  {len(page) // 1024} KB, Drumtonic {fill['@VERSION@']} ({PRODUCT}, sha256 {fill['@SHA@']}...)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
