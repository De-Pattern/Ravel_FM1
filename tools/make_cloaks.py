#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The start screen's picture: the three cloaked figures before the cross and the red circle, cut from the key art
(docs/brand/ravel-key-art.png), its contrast lifted for the small screen, as 16 colours for the FM-1, written to
firmware/app/rv_cloaks.h: an RGB565 palette (index 0: see-through, where the art is near black) and the pixels two
to a byte, row by row.

  tools/make_cloaks.py [WIDTH]     (default 132 pixels wide; the height follows)
"""
import os
import sys

from PIL import Image, ImageEnhance, ImageOps

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W = int(sys.argv[1]) if len(sys.argv) > 1 else 132
CROP = (195, 10, 950, 724)          # the figures, the cross and the circle, in the 2172 x 724 key art
DARK = 30                            # darker than this (0..255): see-through

src = Image.open(os.path.join(ROOT, "docs", "brand", "ravel-key-art.png")).convert("RGB").crop(CROP)
src = ImageOps.autocontrast(src, cutoff=1, preserve_tone=True)
src = src.point(lambda v: int(255 * ((v / 255) ** 0.8)))
H = round(src.size[1] * W / src.size[0])
im = src.resize((W, H), Image.LANCZOS)
q = im.quantize(colors=15, method=Image.MEDIANCUT, dither=Image.NONE)
pal = q.getpalette()[:15 * 3]
idx = list(q.tobytes())
rgb = list(im.getdata()) if not hasattr(im, "get_flattened_data") else list(im.get_flattened_data())
px = [0 if 0.3 * r + 0.59 * g + 0.11 * b < DARK else idx[i] + 1 for i, (r, g, b) in enumerate(rgb)]


def rgb565(r, g, b):
    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3


out = ["/* SPDX-License-Identifier: GPL-3.0-only */",
       "/* the start screen's picture (made by tools/make_cloaks.py from docs/brand/ravel-key-art.png: don't edit) */",
       f"#define CLOAK_W {W}", f"#define CLOAK_H {H}",
       "static const uint16_t cloak_pal[16] = {0x0000, " +
       ", ".join(f"0x{rgb565(*pal[i * 3:i * 3 + 3]):04X}" for i in range(15)) + "};"]
data = bytes((px[k] << 4) | (px[k + 1] if k + 1 < len(px) else 0) for k in range(0, len(px), 2))
out.append(f"static const uint8_t cloak_px[{len(data)}] = {{")
for k in range(0, len(data), 24):
    out.append("    " + ", ".join(f"0x{b:02X}" for b in data[k:k + 24]) + ",")
out.append("};")
with open(os.path.join(ROOT, "firmware", "app", "rv_cloaks.h"), "w") as f:
    f.write("\n".join(out) + "\n")
print(f"rv_cloaks.h: {W} x {H}, {len(data)} bytes")
