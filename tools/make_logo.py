"""Draws the Roulette 47 logo (assets/discord_logo.png, 1024 x 1024).

A clean mark: a thin roulette ring of red and graphite pockets with one white
pocket at the top, a sniper reticle reduced to four red ticks, "47" in the
centre over Agent 47's barcode, and the ROULETTE wordmark. Rendered at 4x and
downscaled for clean edges. Original artwork; no game asset.

    python tools/make_logo.py
"""
import math
import pathlib

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
S = 4096
C = S // 2
RED = (226, 12, 32)
WHITE = (242, 242, 242)
GRAPHITE = (46, 46, 52)
BG = (11, 11, 13)


def font(path, size, variation=None):
    f = ImageFont.truetype(path, size)
    if variation:
        try:
            f.set_variation_by_name(variation)
        except Exception:
            pass
    return f


img = Image.new("RGB", (S, S), BG)

# Faint red halo behind the ring.
halo = Image.new("L", (S, S), 0)
ImageDraw.Draw(halo).ellipse([C - 1500, C - 1500, C + 1500, C + 1500], fill=70)
halo = halo.filter(ImageFilter.GaussianBlur(420))
img.paste(Image.new("RGB", (S, S), (90, 6, 16)), mask=halo)
d = ImageDraw.Draw(img)

# Roulette ring: 37 thin pockets, the top one white.
R_OUT, R_IN = 1700, 1560
n = 37
gap = 1.1
for i in range(n):
    a0 = -90 - 180 / n + i * 360 / n
    colour = WHITE if i == 0 else (RED if i % 2 else GRAPHITE)
    d.pieslice([C - R_OUT, C - R_OUT, C + R_OUT, C + R_OUT], a0 + gap / 2, a0 + 360 / n - gap / 2, fill=colour)
d.ellipse([C - R_IN, C - R_IN, C + R_IN, C + R_IN], fill=BG)

# Inner hairline circle.
R_FIL = 1430
d.ellipse([C - R_FIL, C - R_FIL, C + R_FIL, C + R_FIL], outline=(38, 38, 43), width=10)

# Reticle: four short red ticks.
for ang in (0, 90, 180, 270):
    a = math.radians(ang)
    r0, r1 = R_FIL - 170, R_FIL + 40
    d.line([C + r0 * math.cos(a), C + r0 * math.sin(a), C + r1 * math.cos(a), C + r1 * math.sin(a)], fill=RED, width=30)

# "47".
f47 = font("C:/Windows/Fonts/bahnschrift.ttf", 1500, "Bold Condensed")
b = d.textbbox((0, 0), "47", font=f47)
tw, th = b[2] - b[0], b[3] - b[1]
ty = C - 320 - th / 2 - b[1]
d.text((C - tw / 2 - b[0], ty), "47", font=f47, fill=WHITE)

# Barcode, thin, centred under the number.
bars = [2, 1, 1, 2, 1, 3, 1, 1, 2, 1, 1, 2, 3, 1, 1, 1, 2, 1, 2, 1, 1, 3, 1, 2, 1, 1]
unit = 14
total = sum(bars) * unit + (len(bars) - 1) * unit
x = C - total / 2
y0, y1 = C + 430, C + 600
for i, w in enumerate(bars):
    if i % 2 == 0:
        d.rectangle([x, y0, x + w * unit, y1], fill=WHITE)
    x += w * unit + unit

# Wordmark, widely spaced, in red.
fr = font("C:/Windows/Fonts/bahnschrift.ttf", 230, "SemiBold Condensed")
word = "ROULETTE"
spacing = 95
widths = []
for ch in word:
    bb = d.textbbox((0, 0), ch, font=fr)
    widths.append(bb[2] - bb[0])
total = sum(widths) + spacing * (len(word) - 1)
x = C - total / 2
yw = C + 860
for ch, w in zip(word, widths):
    bb = d.textbbox((0, 0), ch, font=fr)
    d.text((x - bb[0], yw - bb[1]), ch, font=fr, fill=RED)
    x += w + spacing

out = img.resize((1024, 1024), Image.LANCZOS)
out.save(ROOT / "assets" / "discord_logo.png")
print("assets/discord_logo.png")
