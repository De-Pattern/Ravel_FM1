#!/usr/bin/env python3
"""Textures for the emulator redesign: microscopic speckled matte plastic (header, device, dark panels), film grain,
CRT scanlines and vignette. Needs numpy, scipy and Pillow. Writes into assets/."""
import numpy as np
from PIL import Image, ImageFilter

rng = np.random.default_rng(11)


def blur(a, r):
    return np.asarray(Image.fromarray(np.uint8(np.clip(a, 0, 255))).filter(ImageFilter.GaussianBlur(r)), np.float32)


def plastic(w, h, base, name, flecks=0.022, k=1.0, seed=5):
    """Speckled matte plastic, microscopic: rendered at 2x so each fleck is a single texel (well under a
    pixel at 1x). Fine matte grain, faint broad variation, and soft pale-grey micro-flecks."""
    r = np.random.default_rng(seed)
    v = r.normal(0, 1.6, (h, w)) * k + (blur(r.normal(128, 50, (h, w)), 80) - 128) * 0.08 * k
    img = np.dstack([base[c] + v for c in range(3)])
    f = (r.random((h, w)) < flecks) * r.uniform(0.18, 0.55, (h, w))             # 1-texel flecks
    big = (r.random((h, w)) < flecks * 0.08) * r.uniform(0.25, 0.6, (h, w))      # rare 2-texel ones
    f = np.maximum(f, np.maximum(big, np.roll(big, 1, 1)))
    f = np.asarray(Image.fromarray(np.uint8(np.clip(f * 255, 0, 255))).filter(ImageFilter.GaussianBlur(0.3)), np.float32) / 255 * k
    tone = np.array([170, 174, 178], np.float32)
    img = img * (1 - f[..., None]) + tone * f[..., None]
    Image.fromarray(np.uint8(np.clip(img, 0, 255))).save(f"assets/{name}.png", optimize=True)


plastic(2880, 192, (30, 32, 35), "plastic-header")
plastic(2016, 1240, (24, 26, 28), "speckle-device")
plastic(1280, 1280, (20, 22, 24), "plastic-dark", flecks=0.012, k=0.7)

g = rng.normal(0, 1, (720, 720))
a = np.clip(np.abs(g) * 18, 0, 60).astype(np.uint8)
c = np.where(g > 0, 255, 0).astype(np.uint8)
Image.fromarray(np.dstack([c, c, c, a]), "RGBA").save("assets/grain.png", optimize=True)

s = np.zeros((320, 320, 4), np.uint8); s[1::3, :, 3] = 70
Image.fromarray(s, "RGBA").save("assets/scanlines.png", optimize=True)

yy, xx = np.mgrid[0:320, 0:320]
va = np.clip((np.sqrt(((xx - 160) / 160) ** 2 + ((yy - 160) / 160) ** 2) - 0.55) * 260, 0, 200).astype(np.uint8)
Image.fromarray(np.dstack([np.zeros_like(va)] * 3 + [va]), "RGBA").save("assets/vignette.png", optimize=True)

