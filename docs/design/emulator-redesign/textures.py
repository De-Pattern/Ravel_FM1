#!/usr/bin/env python3
"""Textures for the emulator redesign: dark brushed aluminium (header, device, dark panels), film grain,
CRT scanlines and vignette. Needs numpy, scipy and Pillow. Writes into assets/."""
import numpy as np
from PIL import Image, ImageFilter

rng = np.random.default_rng(11)


def blur(a, r):
    return np.asarray(Image.fromarray(np.uint8(np.clip(a, 0, 255))).filter(ImageFilter.GaussianBlur(r)), np.float32)


def brushed(w, h, base, name, k=1.0):
    """Dark anodized aluminium: fine horizontal brushing (noise streaked along x), a soft broad cloud, cool tint."""
    n = rng.normal(0, 1, (h, w * 3))
    st = np.asarray(Image.fromarray(np.uint8(np.clip(n * 40 + 128, 0, 255))).resize((w, h), Image.BILINEAR), np.float32) - 128
    st = np.asarray(Image.fromarray(np.uint8(np.clip(st + 128, 0, 255))).filter(ImageFilter.BoxBlur(0)), np.float32) - 128
    # streak: average along x with a long kernel
    ker = 71
    pad = np.pad(st, ((0, 0), (ker, ker)), mode="wrap")
    cs = np.cumsum(pad, axis=1)
    streak = (cs[:, ker * 2:] - cs[:, :-ker * 2])[:, :w] / (ker * 2)
    v = streak * 1.5 * k + rng.normal(0, 0.6, (h, w)) * k
    v += (blur(rng.normal(128, 50, (h, w)), 80) - 128) * 0.10 * k
    img = np.dstack([base[c] + v * (1.0 + 0.08 * c) for c in range(3)])   # a touch more variation in blue: cold
    Image.fromarray(np.uint8(np.clip(img, 0, 255))).save(f"assets/{name}.png", optimize=True)


brushed(1440, 96, (40, 44, 48), "metal-header")
brushed(1008, 620, (36, 40, 44), "metal-device")
brushed(640, 640, (22, 24, 27), "metal-dark", k=0.6)

g = rng.normal(0, 1, (720, 720))
a = np.clip(np.abs(g) * 18, 0, 60).astype(np.uint8)
c = np.where(g > 0, 255, 0).astype(np.uint8)
Image.fromarray(np.dstack([c, c, c, a]), "RGBA").save("assets/grain.png", optimize=True)

s = np.zeros((320, 320, 4), np.uint8); s[1::3, :, 3] = 70
Image.fromarray(s, "RGBA").save("assets/scanlines.png", optimize=True)

yy, xx = np.mgrid[0:320, 0:320]
va = np.clip((np.sqrt(((xx - 160) / 160) ** 2 + ((yy - 160) / 160) ** 2) - 0.55) * 260, 0, 200).astype(np.uint8)
Image.fromarray(np.dstack([np.zeros_like(va)] * 3 + [va]), "RGBA").save("assets/vignette.png", optimize=True)
