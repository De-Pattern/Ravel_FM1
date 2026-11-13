#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The start screen's animation (docs/brand/ravel-boot.gif: smoke drifting up from the bottom left, RAVEL glowing
out of it, tearing, melting into drips; 16 frames, 220 ms each): eleven of its frames, the lower part of each (the
top is empty), made smaller, in grey levels, and packed to fit the flash, written to firmware/app/rv_boot.h; the
device blends each into the next as it plays.

Each frame is coded row by row, each pixel predicted from its neighbours above and to the left (the median edge
predictor); where all of them are black the pixels go as a run of black (an Elias gamma count, then the pixel that
ends it); elsewhere as what's left over from the prediction (zigzagged, in an adaptive Rice code). The device
unpacks a frame top to bottom as the screen is drawn, keeping only two rows (rv_intro.c, in_bd_*): this encoder
and that decoder must agree bit for bit (`tools/make_boot.py --check` decodes it again here).

  tools/make_boot.py [--check]
"""
import os
import sys

import numpy as np
from PIL import Image, ImageSequence

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
W, H, LV = 180, 117, 16                 # the frames as stored (drawn a third as big again)
SY0 = 100                              # the GIF's rows from here down have anything in them
Y0, Y1 = 78, 239                       # and on the 240 x 240 screen, here
BLACK = 14                             # under this (of 255): black
KEYS = [0, 1, 3, 4, 6, 7, 9, 10, 12, 13, 15]   # the GIF's frames kept
FMS = 220                              # each of its frames, ms


def load():
    im = Image.open(os.path.join(ROOT, "docs", "brand", "ravel-boot.gif"))
    src = []
    for f in ImageSequence.Iterator(im):
        src.append(f.convert("L").copy())
    frames, ms, at = [], [], KEYS + [len(src)]
    for j, i in enumerate(KEYS):
        g = src[i]
        g = g.crop((0, SY0, g.size[0], g.size[1]))
        a = np.asarray(g.resize((W, H), Image.LANCZOS)).astype(float)
        q = np.clip(np.round((a / 255.0) ** 0.7 * (LV - 1)), 0, LV - 1).astype(int)
        q[a < BLACK] = 0
        frames.append(q)
        ms.append((at[j + 1] - i) * FMS)              # (until the next kept; the last, to the end)
    return frames, ms


class Bits:
    def __init__(self):
        self.b = []

    def put(self, v, n):
        for i in range(n - 1, -1, -1):
            self.b.append((v >> i) & 1)

    def bytes(self):
        b = self.b + [0] * (-len(self.b) % 8)
        return bytes(int("".join(map(str, b[i:i + 8])), 2) for i in range(0, len(b), 8))


def med(a, b, c):
    if c >= max(a, b):
        return min(a, b)
    if c <= min(a, b):
        return max(a, b)
    return a + b - c


def kof(A, N):
    k = 0
    while (N << k) < A:
        k += 1
    return k


def encode(q):
    o = Bits()
    A, N = 4, 1
    prev = [0] * (W + 1)
    for y in range(H):
        row = list(q[y])
        x = 0
        while x < W:
            a = row[x - 1] if x else 0
            b, c, d = prev[x], prev[x - 1] if x else 0, prev[x + 1] if x + 1 < W else 0
            if a == 0 and b == 0 and c == 0 and d == 0:
                r = 0
                while x + r < W and row[x + r] == 0:
                    r += 1
                n = r + 1                                  # (Elias gamma of the run + 1)
                L = n.bit_length()
                o.put(0, L - 1)
                o.put(n, L)
                x += r
                if x < W:                                  # (the pixel that ends it: not black)
                    v = row[x] - 1
                    o.put(0, v >> 1)
                    o.put(1, 1)
                    o.put(v & 1, 1)
                    x += 1
                continue
            p = med(a, b, c)
            e = (row[x] - p) % LV
            if e >= LV // 2:
                e -= LV
            m = 2 * e if e >= 0 else -2 * e - 1
            k = kof(A, N)
            o.put(0, m >> k)
            o.put(1, 1)
            o.put(m & ((1 << k) - 1), k)
            A += abs(e)
            N += 1
            if N == 16:
                A = (A + 1) >> 1
                N >>= 1
            x += 1
        prev = row + [0]
    return o.bytes()


def decode(data):
    pos = [0]

    def bit():
        v = (data[pos[0] >> 3] >> (7 - (pos[0] & 7))) & 1
        pos[0] += 1
        return v

    def get(n):
        v = 0
        for _ in range(n):
            v = v << 1 | bit()
        return v
    out = []
    A, N = 4, 1
    prev = [0] * (W + 1)
    for y in range(H):
        row = [0] * W
        x = 0
        while x < W:
            a = row[x - 1] if x else 0
            b, c, d = prev[x], prev[x - 1] if x else 0, prev[x + 1] if x + 1 < W else 0
            if a == 0 and b == 0 and c == 0 and d == 0:
                L = 1
                while not bit():
                    L += 1
                n = 1 << (L - 1) | get(L - 1)
                x += n - 1
                if x < W:
                    q = 0
                    while not bit():
                        q += 1
                    row[x] = (q << 1 | bit()) + 1
                    x += 1
                continue
            k = kof(A, N)
            q = 0
            while not bit():
                q += 1
            m = q << k | get(k)
            e = m >> 1 if not m & 1 else -((m + 1) >> 1)
            row[x] = (med(a, b, c) + e) % LV
            A += abs(e)
            N += 1
            if N == 16:
                A = (A + 1) >> 1
                N >>= 1
            x += 1
        out.append(row)
        prev = row + [0]
    return np.array(out)


def main():
    frames, ms = load()
    blobs = [encode(q) for q in frames]
    if "--check" in sys.argv:
        for q, bl in zip(frames, blobs):
            assert (decode(bl) == q).all()
        print("decodes back exactly")
    offs, tot = [], 0
    for bl in blobs:
        offs.append(tot)
        tot += len(bl)
    out = os.path.join(ROOT, "firmware", "app", "rv_boot.h")
    with open(out, "w") as f:
        f.write("/* SPDX-License-Identifier: GPL-3.0-only */\n")
        f.write("/* the start screen's animation (made by tools/make_boot.py from docs/brand/ravel-boot.gif: don't edit) */\n")
        f.write("#define BOOT_W %d\n#define BOOT_H %d\n#define BOOT_LV %d\n#define BOOT_Y0 %d\n#define BOOT_Y1 %d\n#define BOOT_N %d\n"
                % (W, H, LV, Y0, Y1, len(frames)))
        f.write("static const uint16_t boot_ms[BOOT_N] = {%s};\n" % ", ".join(map(str, ms)))
        f.write("static const uint16_t boot_off[BOOT_N] = {%s};\n" % ", ".join(map(str, offs)))
        lut = []
        for i in range(16 * (LV - 1) + 1):                 # (a level, in sixteenths, to its grey: undoing the curve)
            k = int(round(((i / 16.0) / (LV - 1)) ** 1.25 * 31))   # (a little brighter than undoing it: the faintest, cut to black to pack, are made up for)
            lut.append(k << 11 | (2 * k) << 5 | k)
        f.write("static const uint16_t boot_lut[%d] = {%s};\n" % (len(lut), ", ".join("0x%04X" % v for v in lut)))
        f.write("static const uint8_t boot_bits[%d] = {\n" % tot)
        allb = b"".join(blobs)
        for i in range(0, len(allb), 24):
            f.write("    " + ",".join("0x%02X" % v for v in allb[i:i + 24]) + ",\n")
        f.write("};\n")
    print(out, len(frames), "frames,", tot, "B", [len(b) for b in blobs])


if __name__ == "__main__":
    main()
