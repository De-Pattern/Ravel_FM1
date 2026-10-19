#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""One-file emulator page: web/emu/index.html with ravel.wasm and the manual (tools/make_manual.py, from
README.md) embedded, to build/web/ravel-emulator.html. Without Python's markdown package the page is made
without the manual (its Manual button then opens manual.html beside it)."""
import base64, pathlib, sys

here = pathlib.Path(__file__).resolve().parent
root = here.parent.parent
page = (here / "index.html").read_text()
wasm = base64.b64encode((here / "ravel.wasm").read_bytes()).decode()
assert '"__RAVEL_WASM__"' in page
out = root / "build" / "web" / "ravel-emulator.html"
if len(sys.argv) > 1:
    out = pathlib.Path(sys.argv[1])
out.parent.mkdir(parents=True, exist_ok=True)
page = page.replace('"__RAVEL_WASM__"', '"' + wasm + '"', 1)
sys.path.insert(0, str(root / "tools"))
import ravel_icons
page = ravel_icons.apply(page)
try:
    sys.path.insert(0, str(root / "tools"))
    import make_manual
    manual = make_manual.build(root / "build" / "web" / "ravel-manual.html")
    page = page.replace('"__RAVEL_MANUAL__"', '"' + base64.b64encode(manual.encode("utf-8")).decode() + '"', 1)
except ImportError as e:
    print(f"web: no manual in the page ({e})")
out.write_text(page)
print(f"web: {out} ({out.stat().st_size} B)")
