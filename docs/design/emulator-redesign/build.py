#!/usr/bin/env python3
"""Build ravel-emulator-redesign.fig: inject assets into the Plugin API script, run it with OpenPencil's eval,
then render each top-level frame to PNG for review."""
import base64, json, pathlib, subprocess, sys

here = pathlib.Path(__file__).resolve().parent
out = here / "out"; out.mkdir(exist_ok=True)
assets = {p.stem: base64.b64encode(p.read_bytes()).decode() for p in sorted((here / "assets").glob("*.png"))}
parts = sys.argv[1:] or ["desktop", "components", "motion"]
code = "const ASSETS = " + json.dumps(assets) + ";\n"
code += "for (const n of [...figma.currentPage.children]) n.remove();\nfigma.currentPage.name = 'Ravel emulator';\n"
code += (here / "src" / "lib.js").read_text() + "\n"
for p in parts:
    code += (here / "src" / f"{p}.js").read_text() + "\n"
code += "return figma.currentPage.children.map(n => ({ id: n.id, name: n.name, w: n.width, h: n.height }));\n"
(out / "script.js").write_text(code)
fig = out / "ravel-emulator-redesign.fig"
r = subprocess.run(["openpencil", "eval", str(here / "base.fig"), "--stdin", "-o", str(fig), "--json"], input=code, text=True, capture_output=True)
if r.returncode or "Error" in r.stdout[:400]:
    print(r.stdout[-3000:], r.stderr[-3000:]); sys.exit(1)
for name, slug in [("Emulator — Desktop 1440", "desktop-1440"), ("Components & tokens", "components"), ("Screen motion", "screen-motion")]:
    q = subprocess.run(["openpencil", "find", str(fig), "--name", name, "--json"], capture_output=True, text=True)
    try: nid = json.loads(q.stdout)[0]["id"]
    except Exception: continue
    for scale in ("1", "2"):
        png = out / f"{slug}{'' if scale == '1' else '@2x'}.png"
        subprocess.run(["openpencil", "export", str(fig), "--node", nid, "-s", scale, "-o", str(png)], capture_output=True)
        print(name, "->", png.name)
print("fig:", fig, fig.stat().st_size, "bytes")
