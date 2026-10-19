#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Assemble the GitHub Pages site: the landing page, the emulator, the manual's pictures, the web
installer (web/installer, with fm1pkg.js, fm1ota.js and the package's metadata inlined) and the
firmware itself.

  tools/make_site.py OUT_DIR EMULATOR.html [PACKAGE.fwsc VERSION]

Without a package the site has no installer (the landing page says the firmware is coming)."""
import json
import shutil
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]


def product_of(pkg):
    """the identity a package carries: one marker byte after each of its first 20 blocks of 48 (fm1pkg.js)"""
    out = ""
    for i in range(20):
        m = pkg[i * 0x30 + 0x2F]
        if m != 0x7D:
            out += chr((m - i - 1) & 0xFF)
    return out


def main(argv):
    if len(argv) not in (3, 5):
        raise SystemExit(__doc__)
    out, emu = Path(argv[1]), Path(argv[2])
    if out.exists():
        shutil.rmtree(out)
    (out / "docs").mkdir(parents=True)
    shutil.copy(SRC / "site" / "index.html", out / "index.html")
    shutil.copy(emu, out / "emulator.html")
    manual = emu.parent / "ravel-manual.html"        # made beside the emulator by web/emu/bundle.py
    if manual.exists():
        shutil.copy(manual, out / "manual.html")
    if manual.with_suffix(".pdf").exists():                 # tools/make_manual_pdf.mjs
        shutil.copy(manual.with_suffix(".pdf"), out / "manual.pdf")
    shutil.copy(SRC / "docs" / "signal-flow.svg", out / "signal-flow.svg")
    shutil.copytree(SRC / "docs" / "manual", out / "docs" / "manual")
    (out / ".nojekyll").write_text("")
    if len(argv) == 5:
        pkg_path, version = Path(argv[3]), argv[4]
        pkg = pkg_path.read_bytes()
        product = product_of(pkg)
        if not product.startswith("FM-1_"):
            raise SystemExit(f"{pkg_path}: no FM-1 identity in the package ({product!r})")
        (out / "firmware").mkdir()
        shutil.copy(pkg_path, out / "firmware" / "Ravel-FM1.fwsc")
        lib = "\n".join((SRC / "web" / "installer" / n).read_text(encoding="utf-8") for n in ("fm1pkg.js", "fm1ota.js"))
        lib = "\n".join(ln for ln in lib.splitlines() if not ln.startswith("import ") and not ln.startswith("export {"))
        lib = lib.replace("export function", "function").replace("export class", "class").replace("export const", "const")
        page = (SRC / "web" / "installer" / "index.html").read_text(encoding="utf-8")
        meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/Ravel-FM1.fwsc"})
        for mark in ("/*LIB*/", "/*META*/"):
            if page.count(mark) != 1:
                raise SystemExit(f"web/installer/index.html must contain {mark} once")
        (out / "install").mkdir()
        (out / "install" / "index.html").write_text(page.replace("/*LIB*/", lib).replace("/*META*/", meta),
                                                    encoding="utf-8")
        (out / "firmware" / "latest.json").write_text(json.dumps({"version": version, "product": product}) + "\n")
        print(f"site: installer for {product} ({version})")
    print(f"site: {out}")


if __name__ == "__main__":
    main(sys.argv)
