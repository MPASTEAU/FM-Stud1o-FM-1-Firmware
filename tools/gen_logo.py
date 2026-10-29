#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""FM Studio's logo: a blueberry (after FL Studio's fruit), drawn at 4x then reduced; gen_logo.py OUT (assets/splash.webp)"""
import math, sys
from PIL import Image, ImageDraw, ImageFilter
S = 2048; out = sys.argv[1]
im = Image.new("RGB", (S, S), (8, 8, 16))   # (the screen's BG, T_BG: no square round it)
d = ImageDraw.Draw(im)
# soft glow behind
glow = Image.new("RGB", (S, S), (8, 8, 16)); g = ImageDraw.Draw(glow)
g.ellipse((420, 520, 1630, 1730), fill=(20, 80, 220)); glow = glow.filter(ImageFilter.GaussianBlur(160))
im = Image.blend(im, glow, 0.45); d = ImageDraw.Draw(im)
# the berry: radial gradient
cx, cy, R = 1024, 1150, 560
berry = Image.new("RGB", (S, S)); bm = Image.new("L", (S, S), 0)
ImageDraw.Draw(bm).ellipse((cx - R, cy - R, cx + R, cy + R), fill=255)
px = berry.load()
lx, ly = cx - 230, cy - 260
for y in range(cy - R, cy + R):
    for x in range(cx - R, cx + R):
        t = min(1.0, math.hypot(x - lx, y - ly) / (R * 1.55))
        a = (90, 200, 255); b = (20, 90, 255); c = (10, 24, 110)   # (an electric blue)
        if t < 0.5:
            k = t / 0.5; col = tuple(int(a[i] + (b[i] - a[i]) * k) for i in range(3))
        else:
            k = (t - 0.5) / 0.5; col = tuple(int(b[i] + (c[i] - b[i]) * k) for i in range(3))
        px[x, y] = col
im.paste(berry, (0, 0), bm); d = ImageDraw.Draw(im)
# bloom (the berry's dusty sheen): a pale rim on the left
rim = Image.new("L", (S, S), 0); ImageDraw.Draw(rim).ellipse((cx - R, cy - R, cx + R, cy + R), outline=255, width=26)
rim = rim.filter(ImageFilter.GaussianBlur(18))
im.paste((150, 180, 255), (0, 0), Image.eval(rim, lambda v: v * 3 // 10))
# highlight
hl = Image.new("L", (S, S), 0); ImageDraw.Draw(hl).ellipse((cx - 330, cy - 400, cx - 120, cy - 250), fill=255)
hl = hl.filter(ImageFilter.GaussianBlur(30)); im.paste((230, 240, 255), (0, 0), Image.eval(hl, lambda v: v * 8 // 10))
d = ImageDraw.Draw(im)
# the crown: a five-pointed calyx at the top
ccx, ccy, r0, r1 = cx + 40, cy - R + 70, 70, 175
pts = []
for i in range(10):
    ang = -math.pi / 2 + i * math.pi / 5; r = r1 if i % 2 == 0 else r0
    pts.append((ccx + r * math.cos(ang), ccy + r * 0.62 * math.sin(ang)))
d.polygon(pts, fill=(18, 20, 64))
d.ellipse((ccx - 62, ccy - 40, ccx + 62, ccy + 40), fill=(10, 12, 40))
# two leaves and a stem
def leaf(x0, y0, x1, y1, w, col, vein):
    n = 40; L = []; R2 = []
    for i in range(n + 1):
        t = i / n; x = x0 + (x1 - x0) * t; y = y0 + (y1 - y0) * t
        nx, ny = -(y1 - y0), (x1 - x0); l = math.hypot(nx, ny); nx /= l; ny /= l
        s = w * math.sin(math.pi * t)
        L.append((x + nx * s, y + ny * s)); R2.append((x - nx * s, y - ny * s))
    d.polygon(L + R2[::-1], fill=col); d.line((x0, y0, x1, y1), fill=vein, width=10)
d.line((ccx + 10, ccy - 40, ccx + 70, ccy - 210), fill=(40, 170, 50), width=34)
leaf(ccx + 70, ccy - 200, ccx + 390, ccy - 330, 95, (40, 215, 60), (25, 150, 40))
leaf(ccx + 60, ccy - 205, ccx - 200, ccy - 380, 80, (40, 215, 60), (25, 150, 40))
im = im.resize((512, 512), Image.LANCZOS)
im.save(out)
