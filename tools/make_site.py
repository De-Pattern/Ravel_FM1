#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Assemble the GitHub Pages site: the emulator as the front page, the manual (web page and PDF), the web
installer (web/installer, with fm1pkg.js, fm1ota.js and the package's metadata inlined) and the
firmware itself.

  tools/make_site.py OUT_DIR EMULATOR.html [PACKAGE.fwsc VERSION [RELEASES_DIR]]

Without a package the site has no installer. RELEASES_DIR holds list.txt ("TAG DATE" a line, newest
first) and TAG/Ravel-FM1.fwsc for each: the installer offers those releases too, the newest first,
with this commit's build first, as the test build, when it is not one of them."""
import json
import shutil
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SRC / "tools"))


def product_of(pkg):
    """the identity a package carries: one marker byte after each of its first 20 blocks of 48 (fm1pkg.js)"""
    out = ""
    for i in range(20):
        m = pkg[i * 0x30 + 0x2F]
        if m != 0x7D:
            out += chr((m - i - 1) & 0xFF)
    return out


def main(argv):
    if len(argv) not in (3, 5, 6):
        raise SystemExit(__doc__)
    out, emu = Path(argv[1]), Path(argv[2])
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    shutil.copy(SRC / "docs" / "brand" / "favicon.ico", out / "favicon.ico")
    shutil.copy(SRC / "docs" / "brand" / "apple-touch-icon.png", out / "apple-touch-icon.png")
    shutil.copy(emu, out / "index.html")               # the site opens on the emulator
    shutil.copy(emu, out / "emulator.html")            # (and the older address still works)
    manual = emu.parent / "ravel-manual.html"        # made beside the emulator by web/emu/bundle.py
    if manual.exists():
        shutil.copy(manual, out / "manual.html")
    if manual.with_suffix(".pdf").exists():                 # tools/make_manual_pdf.mjs
        shutil.copy(manual.with_suffix(".pdf"), out / "manual.pdf")
    (out / ".nojekyll").write_text("")
    if len(argv) >= 5:
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
        try:                                                # the commit's date, shown with the version
            import subprocess
            built = subprocess.run(["git", "log", "-1", "--format=%cs"], capture_output=True, text=True, cwd=SRC).stdout.strip()
        except OSError:
            built = ""
        builds = []                                         # the releases, newest first, then the test build
        rel = Path(argv[5]) if len(argv) == 6 else None
        if rel and (rel / "list.txt").exists():
            for line in (rel / "list.txt").read_text().splitlines():
                parts = line.split()
                if len(parts) < 2 or not (rel / parts[0] / "Ravel-FM1.fwsc").exists():
                    continue
                tag, date = parts[0], parts[1]
                rp = (rel / tag / "Ravel-FM1.fwsc").read_bytes()
                if not product_of(rp).startswith("FM-1_"):
                    continue
                (out / "firmware" / tag).mkdir()
                (out / "firmware" / tag / "Ravel-FM1.fwsc").write_bytes(rp)
                builds.append({"version": tag, "product": product_of(rp), "built": date, "release": True,
                               "pkg": f"../firmware/{tag}/Ravel-FM1.fwsc"})
        if not any(b["version"].lstrip("v") == version.lstrip("v") for b in builds):
            builds.insert(0, {"version": version, "product": product, "built": built, "release": False,
                           "pkg": "../firmware/Ravel-FM1.fwsc"})
        builds = builds[:11]
        meta = json.dumps({"builds": builds})
        for mark in ("/*LIB*/", "/*META*/"):
            if page.count(mark) != 1:
                raise SystemExit(f"web/installer/index.html must contain {mark} once")
        page = __import__("ravel_icons").apply(page)
        (out / "install").mkdir()
        (out / "install" / "index.html").write_text(page.replace("/*LIB*/", lib).replace("/*META*/", meta),
                                                    encoding="utf-8")
        latest = next((b["version"] for b in builds if b["release"]), None)
        (out / "firmware" / "latest.json").write_text(json.dumps({"version": version, "product": product,
                                                                  "release": latest}) + "\n")
        print(f"site: installer for {product} ({version}); releases: {', '.join(b['version'] for b in builds if b['release']) or 'none'}")
    print(f"site: {out}")


if __name__ == "__main__":
    main(sys.argv)
