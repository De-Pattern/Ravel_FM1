#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The start screen's data (firmware/app/rv_boot.h): the RAVEL wordmark, 224 x 46 at one bit a pixel
(docs/brand/ravel-logo-224x46.bin: row by row, the leftmost pixel the top bit), and how far each of its rows is
pushed sideways in each frame of the smear at the end. The device draws the 72 frames (24 a second, three seconds)
from these with a few rules (rv_intro.c, in_logo_row); the smear's shifts are worked out here, in double precision,
so the FM-1 (single precision) gives the same pixels.

The frames, from the start: the wordmark comes in band by band from the top (its left side cut away at first, a
two-row gap every fourth frame); settles (a two-row gap once); holds; then from frame 55 its rows slide, more and
more (two sines), and from 62 every eighth pair of rows tears further; frame 71 is black. Thin grey lines flash on
eight of the frames.

  tools/make_boot.py
"""
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LW, LH = 224, 46
S0, S1 = 55, 71                         # the frames with shifts: 55 .. 70


def shifts(i):
    p = (i - S0) / 17
    out = []
    for y in range(LH):
        dx = int(round((p ** 1.6) * (6 * math.sin(y * .42 + p * 13) + 10 * math.sin(y * .12 + p * 19))))
        if i >= 62 and y % 8 in (0, 1) and (i + y) % 5 < 3:
            dx += int(5 + 20 * p)
        out.append(dx % LW if dx % LW < 128 else dx % LW - LW)   # (a roll: kept small, either way)
    return out


def main():
    logo = open(os.path.join(ROOT, "docs", "brand", "ravel-logo-224x46.bin"), "rb").read()
    assert len(logo) == LW * LH // 8
    out = os.path.join(ROOT, "firmware", "app", "rv_boot.h")
    with open(out, "w") as f:
        f.write("/* SPDX-License-Identifier: GPL-3.0-only */\n")
        f.write("/* the start screen's wordmark and its smear (made by tools/make_boot.py: don't edit) */\n")
        f.write("#define BOOT_LW %d\n#define BOOT_LH %d\n#define BOOT_S0 %d\n#define BOOT_S1 %d\n" % (LW, LH, S0, S1))
        f.write("static const uint8_t boot_logo[%d] = {\n" % len(logo))
        for i in range(0, len(logo), 28):
            f.write("    " + ",".join("0x%02X" % v for v in logo[i:i + 28]) + ",\n")
        f.write("};\n")
        f.write("static const int8_t boot_shift[%d][%d] = {\n" % (S1 - S0, LH))
        for i in range(S0, S1):
            f.write("    {" + ",".join(str(v) for v in shifts(i)) + "},\n")
        f.write("};\n")
    print(out, len(logo), "+", (S1 - S0) * LH, "B")


if __name__ == "__main__":
    main()
