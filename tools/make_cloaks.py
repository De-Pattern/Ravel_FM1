#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The start screen's three cloaked figures: docs/brand/cloaks.png (pixel art, transparent around them) as a 16-colour
picture for the FM-1, written to firmware/app/rv_cloaks.h: an RGB565 palette (index 0: see-through) and the
pixels two to a byte, row by row.

  tools/make_cloaks.py [WIDTH]     (default 132 pixels wide; the height follows)
"""
import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W = int(sys.argv[1]) if len(sys.argv) > 1 else 132

src = Image.open(os.path.join(ROOT, "docs", "brand", "cloaks.png")).convert("RGBA")
src = src.crop(src.getbbox())
H = round(src.size[1] * W / src.size[0])
im = src.resize((W, H), Image.LANCZOS)
alpha = im.split()[3]
rgb = Image.new("RGB", im.size, (0, 0, 0))
rgb.paste(im, mask=alpha)
q = rgb.quantize(colors=15, method=Image.MEDIANCUT, dither=Image.NONE)
pal = q.getpalette()[:15 * 3]
idx = list(q.tobytes())
amask = list(alpha.tobytes())
px = [0 if amask[i] < 128 else idx[i] + 1 for i in range(W * H)]   # 0: see-through


def rgb565(r, g, b):
    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3


out = []
out.append("/* SPDX-License-Identifier: GPL-3.0-only */")
out.append("/* the start screen's three cloaked figures (made by tools/make_cloaks.py from docs/brand/cloaks.png: don't edit) */")
out.append(f"#define CLOAK_W {W}")
out.append(f"#define CLOAK_H {H}")
out.append("static const uint16_t cloak_pal[16] = {0x0000, " +
           ", ".join(f"0x{rgb565(*pal[i * 3:i * 3 + 3]):04X}" for i in range(15)) + "};")
data = bytes((px[k] << 4) | (px[k + 1] if k + 1 < len(px) else 0) for k in range(0, len(px), 2))
out.append(f"static const uint8_t cloak_px[{len(data)}] = {{")
for k in range(0, len(data), 24):
    out.append("    " + ", ".join(f"0x{b:02X}" for b in data[k:k + 24]) + ",")
out.append("};")
with open(os.path.join(ROOT, "firmware", "app", "rv_cloaks.h"), "w") as f:
    f.write("\n".join(out) + "\n")
print(f"rv_cloaks.h: {W} x {H}, {len(data)} bytes")
