#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""FM Stud1o's logo: a blueberry (after FL Studio's fruit), drawn at 4x then reduced, in ICE's colours (TEXT, THEME,
ACCENT): the fruit alone, no glow, no rim, no background (transparent: the splash shows the screen's BG there,
tools/gen_splash_image.py; the pages their own).
  gen_logo.py [OUT]   OUT (default assets/splash.webp, with its alpha), and the pages' icon (a 64 x 64 PNG):
                      web/editor.html's and web/emu/index.html's <link rel="icon"> data URI"""
import base64, io, math, re, sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter
ROOT = Path(__file__).resolve().parent.parent
S = 2048; out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "assets" / "splash.webp"
im = Image.new("RGBA", (S, S), (0, 0, 0, 0))   # (nothing round it)
# the berry: radial gradient
cx, cy, R = 1024, 1150, 560
berry = Image.new("RGB", (S, S)); bm = Image.new("L", (S, S), 0)
ImageDraw.Draw(bm).ellipse((cx - R, cy - R, cx + R, cy + R), fill=255)
px = berry.load()
lx, ly = cx - 230, cy - 260
for y in range(cy - R, cy + R):
    for x in range(cx - R, cx + R):
        t = min(1.0, math.hypot(x - lx, y - ly) / (R * 1.55))
        a = (224, 240, 248); b = (80, 184, 232); c = (14, 44, 76)   # (ICE: TEXT, THEME, a deep THEME)
        if t < 0.5:
            k = t / 0.5; col = tuple(int(a[i] + (b[i] - a[i]) * k) for i in range(3))
        else:
            k = (t - 0.5) / 0.5; col = tuple(int(b[i] + (c[i] - b[i]) * k) for i in range(3))
        px[x, y] = col
im.paste(berry, (0, 0), bm)
# highlight (inside the berry only)
hl = Image.new("L", (S, S), 0); ImageDraw.Draw(hl).ellipse((cx - 330, cy - 400, cx - 120, cy - 250), fill=255)
hl = hl.filter(ImageFilter.GaussianBlur(30))
hl = Image.eval(hl, lambda v: v * 8 // 10)
hl = Image.composite(hl, Image.new("L", (S, S), 0), bm)
im.paste((236, 246, 250, 255), (0, 0), hl)
d = ImageDraw.Draw(im)
# the crown: a five-pointed calyx at the top
ccx, ccy, r0, r1 = cx + 40, cy - R + 70, 70, 175
pts = []
for i in range(10):
    ang = -math.pi / 2 + i * math.pi / 5; r = r1 if i % 2 == 0 else r0
    pts.append((ccx + r * math.cos(ang), ccy + r * 0.62 * math.sin(ang)))
d.polygon(pts, fill=(12, 34, 52))
d.ellipse((ccx - 62, ccy - 40, ccx + 62, ccy + 40), fill=(8, 22, 36))
# two leaves and a stem
def leaf(x0, y0, x1, y1, w, col, vein):
    n = 40; L = []; R2 = []
    for i in range(n + 1):
        t = i / n; x = x0 + (x1 - x0) * t; y = y0 + (y1 - y0) * t
        nx, ny = -(y1 - y0), (x1 - x0); l = math.hypot(nx, ny); nx /= l; ny /= l
        s = w * math.sin(math.pi * t)
        L.append((x + nx * s, y + ny * s)); R2.append((x - nx * s, y - ny * s))
    d.polygon(L + R2[::-1], fill=col); d.line((x0, y0, x1, y1), fill=vein, width=10)
d.line((ccx + 10, ccy - 40, ccx + 70, ccy - 210), fill=(200, 108, 72), width=34)   # (ICE's ACCENT)
leaf(ccx + 70, ccy - 200, ccx + 390, ccy - 330, 95, (248, 140, 96), (190, 96, 62))
leaf(ccx + 60, ccy - 205, ccx - 200, ccy - 380, 80, (248, 140, 96), (190, 96, 62))
small = im.resize((512, 512), Image.LANCZOS)
small.save(out, quality=90, alpha_quality=100, method=6)
ico = io.BytesIO()
im.resize((64, 64), Image.LANCZOS).save(ico, "PNG", optimize=True)
uri = "data:image/png;base64," + base64.b64encode(ico.getvalue()).decode()
for page in (ROOT / "web" / "editor.html", ROOT / "web" / "emu" / "index.html"):
    text = page.read_text()
    new = re.sub(r'(<link rel="icon" type="image/png" href=")data:image/png;base64,[A-Za-z0-9+/=]*(")', lambda m: m.group(1) + uri + m.group(2), text, count=1)
    if new != text:
        page.write_text(new)
print(f"logo: {out} 512 x 512 (transparent round the fruit), the pages' icon 64 x 64 ({len(ico.getvalue())} B)")
