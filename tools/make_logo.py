#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The start screen's RAVEL wordmark (docs/brand/ravel-outline.png): the letters drawn from their shapes, wide and
geometric, the A without its bar, each a hollow outline one pixel wide at the size the screen shows them, written to
firmware/app/rv_logo.h as one bit a pixel (row by row, the first pixel in the top bit).

  tools/make_logo.py [--png out.png]
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TW = 216                               # on the screen: this wide (and its height to match)
S = 4                                  # drawn this much bigger, then made smaller


def letters(d):
    """the letters, filled, in the artwork's units (the word from x 222 to 1272, y 455 to 590)"""
    def P(pts):
        return [((x - 222) * S, (y - 455) * S) for x, y in pts]
    # R: the stem, the bowl with its rounded corners (and its hole), the leg
    d.rectangle(P([(222, 455), (255, 590)]), fill=1)
    d.rounded_rectangle(P([(222, 455), (425, 545)]), radius=38 * S, fill=1)
    d.rounded_rectangle(P([(255, 487), (393, 512)]), radius=8 * S, fill=0)
    d.polygon(P([(345, 545), (390, 545), (425, 590), (380, 590)]), fill=1)
    # A: two legs to a point, no bar
    d.polygon(P([(452, 590), (545, 455), (575, 455), (668, 590), (626, 590), (560, 493), (494, 590)]), fill=1)
    # V
    d.polygon(P([(640, 455), (684, 455), (752, 555), (820, 455), (864, 455), (771, 590), (733, 590)]), fill=1)
    # E
    for r in ((890, 455, 1058, 487), (890, 455, 922, 590), (922, 507, 1042, 540), (890, 558, 1060, 590)):
        d.rectangle(P([r[:2], r[2:]]), fill=1)
    # L
    d.rectangle(P([(1098, 455), (1130, 590)]), fill=1)
    d.rectangle(P([(1098, 558), (1272, 590)]), fill=1)


def outline():
    w, h = (1272 - 222) * S, 135 * S
    im = Image.new("L", (w + S, h + S), 0)
    letters(ImageDraw.Draw(im))
    th = round(TW * 135 / 1050)
    a = np.asarray(im.point(lambda v: v * 255).resize((TW, th), Image.LANCZOS)) > 110
    p = np.pad(a, 1)
    inner = p[:-2, 1:-1] & p[2:, 1:-1] & p[1:-1, :-2] & p[1:-1, 2:]
    return a & ~inner


def main():
    o = outline()
    h, w = o.shape
    bpr = (w + 7) // 8
    rows = np.zeros((h, bpr * 8), dtype=np.uint8)
    rows[:, :w] = o
    data = np.packbits(rows, axis=1).flatten()
    out = ["/* SPDX-License-Identifier: GPL-3.0-only */",
           "/* The start screen's RAVEL: an outline, one bit a pixel, row by row (written by tools/make_logo.py) */",
           f"#define LOGO_W {w}", f"#define LOGO_H {h}", f"#define LOGO_BPR {bpr}",
           f"static const uint8_t logo_bits[{len(data)}] = {{"]
    for i in range(0, len(data), 18):
        out.append("    " + ", ".join(f"0x{v:02x}" for v in data[i:i + 18]) + ",")
    out.append("};")
    with open(os.path.join(ROOT, "firmware", "app", "rv_logo.h"), "w") as f:
        f.write("\n".join(out) + "\n")
    if "--png" in sys.argv:
        Image.fromarray((o * 255).astype(np.uint8)).resize((w * 4, h * 4), Image.NEAREST).save(sys.argv[sys.argv.index("--png") + 1])
    print(f"rv_logo.h: {w} x {h}, {int(o.sum())} pixels, {len(data)} B")


if __name__ == "__main__":
    main()
