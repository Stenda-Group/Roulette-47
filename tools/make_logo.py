"""Draws the Roulette 47 logo (assets/discord_logo.png, 1024 x 1024).

A roulette wheel (alternating red and black pockets) around a sniper reticle,
"47" in the centre, Agent 47's barcode under it and the ROULETTE wordmark.
Rendered at 4x and downscaled for clean edges. Original artwork; no game asset.

    python tools/make_logo.py
"""
import math
import pathlib

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
S = 4096                     # working size (4x)
C = S // 2
RED = (225, 10, 30)
RED_DARK = (120, 6, 18)
BLACK = (14, 14, 16)
INK = (24, 24, 27)
WHITE = (244, 244, 244)
GREY = (70, 70, 76)

img = Image.new("RGB", (S, S), BLACK)
d = ImageDraw.Draw(img)

# Soft red glow behind the wheel.
glow = Image.new("RGB", (S, S), BLACK)
gd = ImageDraw.Draw(glow)
gd.ellipse([C - 1700, C - 1700, C + 1700, C + 1700], fill=(70, 4, 12))
glow = glow.filter(ImageFilter.GaussianBlur(260))
img.paste(glow)
d = ImageDraw.Draw(img)

# Outer rim.
R_OUT, R_POCKET, R_IN = 1880, 1760, 1380
d.ellipse([C - R_OUT, C - R_OUT, C + R_OUT, C + R_OUT], fill=(34, 34, 38))
d.ellipse([C - R_OUT + 26, C - R_OUT + 26, C + R_OUT - 26, C + R_OUT - 26], fill=(20, 20, 23))

# Pockets: 37 alternating red / black, the zero in white (the "47" slot).
n = 37
for i in range(n):
    a0 = -90 + i * 360 / n
    a1 = a0 + 360 / n
    colour = WHITE if i == 0 else (RED if i % 2 else INK)
    d.pieslice([C - R_POCKET, C - R_POCKET, C + R_POCKET, C + R_POCKET], a0 + .6, a1 - .6, fill=colour)
d.ellipse([C - R_IN, C - R_IN, C + R_IN, C + R_IN], fill=BLACK)

# Thin separators ring.
for r, w, col in ((R_POCKET + 6, 14, (52, 52, 58)), (R_IN, 18, (52, 52, 58))):
    d.ellipse([C - r, C - r, C + r, C + r], outline=col, width=w)

# Ball, resting on the white pocket.
ball_r = 90
ba = math.radians(-90 + 180 / n)
bx, by = C + (R_POCKET - 200) * math.cos(ba), C + (R_POCKET - 200) * math.sin(ba)
d.ellipse([bx - ball_r, by - ball_r, bx + ball_r, by + ball_r], fill=WHITE)
d.ellipse([bx - ball_r + 34, by - ball_r + 24, bx - 10, by - 20], fill=(255, 255, 255))

# Reticle inside the wheel.
R_RET = 1180
d.ellipse([C - R_RET, C - R_RET, C + R_RET, C + R_RET], outline=GREY, width=22)
d.ellipse([C - 860, C - 860, C + 860, C + 860], outline=(44, 44, 49), width=10)
for (x0, y0, x1, y1) in ((C, C - R_RET - 60, C, C - 930), (C, C + 930, C, C + R_RET + 60),
                         (C - R_RET - 60, C, C - 930, C), (C + 930, C, C + R_RET + 60, C)):
    d.line([x0, y0, x1, y1], fill=RED, width=30)
for k in range(12):
    a = math.radians(k * 30)
    r0, r1 = R_RET - 70, R_RET - 10
    d.line([C + r0 * math.cos(a), C + r0 * math.sin(a), C + r1 * math.cos(a), C + r1 * math.sin(a)], fill=GREY, width=14)

# "47".
f47 = ImageFont.truetype("C:/Windows/Fonts/impact.ttf", 1450)
b = d.textbbox((0, 0), "47", font=f47)
tw, th = b[2] - b[0], b[3] - b[1]
tx, ty = C - tw / 2 - b[0], C - th / 2 - b[1] - 170
shadow = Image.new("L", (S, S), 0)
ImageDraw.Draw(shadow).text((tx + 40, ty + 50), "47", font=f47, fill=200)
shadow = shadow.filter(ImageFilter.GaussianBlur(40))
img.paste(Image.new("RGB", (S, S), (0, 0, 0)), mask=shadow)
d = ImageDraw.Draw(img)
d.text((tx, ty), "47", font=f47, fill=WHITE)

# Barcode under the number (Agent 47's tattoo, drawn freely).
bars = [3, 1, 2, 1, 1, 3, 1, 2, 2, 1, 1, 1, 3, 2, 1, 1, 2, 3, 1, 1, 2, 1, 3, 1, 1, 2, 1, 2]
unit = 18
total = sum(bars) * unit + (len(bars) - 1) * unit
x = C - total / 2
y0, y1 = C + 560, C + 800
for i, w in enumerate(bars):
    if i % 2 == 0:
        d.rectangle([x, y0, x + w * unit, y1], fill=WHITE)
    x += w * unit + unit

# Red band with the wordmark.
band_y = S - 820
d.rectangle([0, band_y, S, band_y + 470], fill=RED)
d.rectangle([0, band_y - 18, S, band_y], fill=RED_DARK)
fr = ImageFont.truetype("C:/Windows/Fonts/bahnschrift.ttf", 360)
try:
    fr.set_variation_by_name("Bold Condensed")
except Exception:
    pass
word = "ROULETTE"
spacing = 70
widths = [d.textbbox((0, 0), ch, font=fr)[2] - d.textbbox((0, 0), ch, font=fr)[0] for ch in word]
total = sum(widths) + spacing * (len(word) - 1)
x = C - total / 2
for ch, w in zip(word, widths):
    b = d.textbbox((0, 0), ch, font=fr)
    d.text((x - b[0], band_y + 235 - (b[3] - b[1]) / 2 - b[1]), ch, font=fr, fill=WHITE)
    x += w + spacing

out = img.resize((1024, 1024), Image.LANCZOS)
out.save(ROOT / "assets" / "discord_logo.png")
print("assets/discord_logo.png")
