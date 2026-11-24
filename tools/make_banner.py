"""Generates the animated README artwork (assets/banner.svg, assets/modes.svg).

The ROULETTE 47 lettering is drawn here as stroked paths on a simple geometric
grid (thick strokes, flat ends, wide spacing) in the spirit of the HITMAN
logotype; no font file or third-party artwork is used. Each letter is drawn
on with a stroke-dashoffset animation.

    python tools/make_banner.py
"""
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"

RED = "#e10a1e"
WHITE = "#f4f4f4"

# Centre lines of each glyph, cap height 120, stroke 22 (so the outer edges
# sit at y = 0 and y = 120). Widths are advance widths before tracking.
GLYPHS = {
    "R": (82, "M11,109 V11 H57 Q71,11 71,25 V45 Q71,59 57,59 H11 M42,59 L75,109"),
    "O": (90, "M25,11 H65 Q79,11 79,25 V95 Q79,109 65,109 H25 Q11,109 11,95 V25 Q11,11 25,11 Z"),
    "U": (88, "M11,0 V95 Q11,109 25,109 H63 Q77,109 77,95 V0"),
    "L": (70, "M11,0 V109 H70"),
    "E": (74, "M74,11 H11 V109 H74 M11,60 H60"),
    "T": (84, "M0,11 H84 M42,11 V120"),
    "4": (76, "M54,120 V11 L8,80 H76"),
    "7": (70, "M0,11 H66 L28,120"),
}
TRACKING = 30


def word(text, x0, y0, colour, start, step, cls):
    out, x = [], x0
    for i, ch in enumerate(text):
        w, d = GLYPHS[ch]
        delay = start + i * step
        out.append(
            f'<path class="{cls}" d="{d}" transform="translate({x} {y0})" stroke="{colour}" '
            f'style="animation-delay:{delay:.2f}s"/>'
        )
        x += w + TRACKING
    return "\n    ".join(out), x - TRACKING


def barcode(x0, y0, height):
    # A 47-style barcode: fixed pattern, so the artwork is stable.
    widths = [3, 1, 2, 1, 1, 3, 1, 2, 2, 1, 3, 1, 1, 2, 1, 3, 2, 1, 1, 2, 3, 1, 2, 1, 1, 3, 1, 1, 2]
    out, x = [], x0
    for i, w in enumerate(widths):
        if i % 2 == 0:
            out.append(f'<rect class="bar" x="{x}" y="{y0}" width="{w * 3}" height="{height}" style="animation-delay:{0.15 + i * 0.03:.2f}s"/>')
        x += w * 3
    return "\n    ".join(out), x


def banner():
    W, H = 1280, 420
    letters, end = word("ROULETTE", 0, 0, WHITE, 0.35, 0.11, "glyph")
    num, end47 = word("47", end + 56, 0, RED, 0.35 + 8 * 0.11 + 0.1, 0.14, "glyph glyph47")
    total = end47
    scale = 0.95
    ox = (W - total * scale) / 2
    oy = 128
    bars, bx = barcode(0, 0, 46)

    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" role="img" aria-label="Roulette 47">
  <title>Roulette 47</title>
  <style>
    .glyph {{ fill: none; stroke-width: 22; stroke-linecap: butt; stroke-linejoin: miter;
             stroke-dasharray: 520; stroke-dashoffset: 520; animation: draw 1.1s cubic-bezier(.6,.05,.25,1) forwards; }}
    .glyph47 {{ filter: url(#glow); }}
    .bar {{ fill: #8a8a92; opacity: 0; animation: bar .25s ease-out forwards; }}
    .sweep {{ transform-box: fill-box; transform-origin: left; transform: scaleX(0); animation: sweep 1.4s 1.5s cubic-bezier(.2,.8,.2,1) forwards; }}
    .tag {{ opacity: 0; animation: up .8s 2.1s cubic-bezier(.2,.8,.2,1) forwards; }}
    .tag2 {{ opacity: 0; animation: up .8s 2.35s cubic-bezier(.2,.8,.2,1) forwards; }}
    .scan {{ animation: scan 6s 3s ease-in-out infinite; opacity: 0; }}
    .reticle {{ transform-origin: 1150px 66px; animation: spin 14s linear infinite, fade 1s 2.4s forwards; opacity: 0; }}
    .grid {{ opacity: .07; }}
    @keyframes draw {{ to {{ stroke-dashoffset: 0; }} }}
    @keyframes bar {{ to {{ opacity: .85; }} }}
    @keyframes sweep {{ to {{ transform: scaleX(1); }} }}
    @keyframes up {{ from {{ opacity: 0; transform: translateY(10px); }} to {{ opacity: 1; transform: translateY(0); }} }}
    @keyframes fade {{ to {{ opacity: .55; }} }}
    @keyframes spin {{ to {{ transform: rotate(360deg); }} }}
    @keyframes scan {{ 0% {{ opacity: 0; transform: translateX(-260px); }} 15% {{ opacity: .9; }} 45% {{ opacity: 0; transform: translateX({W}px); }} 100% {{ opacity: 0; transform: translateX({W}px); }} }}
  </style>
  <defs>
    <radialGradient id="bg" cx="50%" cy="38%" r="75%">
      <stop offset="0" stop-color="#2a0a0e"/>
      <stop offset=".45" stop-color="#110b0c"/>
      <stop offset="1" stop-color="#070708"/>
    </radialGradient>
    <linearGradient id="scanGrad" x1="0" x2="1">
      <stop offset="0" stop-color="{RED}" stop-opacity="0"/>
      <stop offset=".5" stop-color="{RED}" stop-opacity=".35"/>
      <stop offset="1" stop-color="{RED}" stop-opacity="0"/>
    </linearGradient>
    <pattern id="lines" width="28" height="28" patternUnits="userSpaceOnUse" patternTransform="rotate(32)">
      <rect width="1" height="28" fill="#ffffff"/>
    </pattern>
    <filter id="glow" x="-30%" y="-30%" width="160%" height="160%">
      <feGaussianBlur stdDeviation="3.2" result="b"/>
      <feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge>
    </filter>
  </defs>

  <rect width="{W}" height="{H}" fill="url(#bg)"/>
  <rect class="grid" width="{W}" height="{H}" fill="url(#lines)"/>
  <rect x="0" y="0" width="{W}" height="6" fill="{RED}"/>

  <!-- 47's barcode -->
  <g transform="translate(64 52)">
    {bars}
  </g>

  <!-- Reticle -->
  <g class="reticle" fill="none" stroke="{RED}" stroke-width="2.5">
    <circle cx="1150" cy="66" r="26"/>
    <circle cx="1150" cy="66" r="6" fill="{RED}" stroke="none"/>
    <path d="M1150 30 V48 M1150 84 V102 M1114 66 H1132 M1168 66 H1186"/>
  </g>

  <!-- ROULETTE 47 -->
  <g transform="translate({ox:.1f} {oy}) scale({scale})">
    {letters}
    {num}
  </g>

  <!-- Red rule -->
  <rect class="sweep" x="{ox:.1f}" y="{oy + 150}" width="{total * scale:.1f}" height="5" fill="{RED}"/>

  <text class="tag" x="{W / 2}" y="{oy + 205}" text-anchor="middle" fill="#d9d9de"
        font-family="Segoe UI, Helvetica Neue, Arial, sans-serif" font-size="26" font-weight="600" letter-spacing="7">CONTRACT ROULETTE FOR HITMAN: WORLD OF ASSASSINATION</text>
  <text class="tag2" x="{W / 2}" y="{oy + 245}" text-anchor="middle" fill="#8f8f98"
        font-family="Segoe UI, Helvetica Neue, Arial, sans-serif" font-size="19" letter-spacing="3">Destination · Targets · Methods · Outfits · Complications · Loadout — drawn in one spin</text>

  <rect class="scan" x="0" y="0" width="260" height="{H}" fill="url(#scanGrad)"/>
</svg>
"""


def modes():
    W, H = 1280, 300
    cards = [
        ("ASSASSIN", "DEFAULT", "The classic hitman.",
         ["Agent 47's suit, suited start", "Silenced Silverballer · fiber wire · lockpick",
          "Pistol, fiber wire, accident or poison", "Realistic complications"]),
        ("PURIST", "", "Silent Assassin, Suit Only.",
         ["Agent 47's suit, no other outfit", "No gun: fiber wire and coins",
          "Accidents, poison or fiber wire", "Unseen · no bodies · no victims · no cameras"]),
        ("FREESTYLE", "", "Anything goes.",
         ["Random methods and outfits", "Random starting location and gear", "Driven by your settings"]),
    ]
    gap, pad = 24, 32
    cw = (W - 2 * pad - 2 * gap) / 3
    out = []
    for i, (name, tag, hook, lines) in enumerate(cards):
        x = pad + i * (cw + gap)
        d = 0.15 + i * 0.25
        active = i == 0
        border = RED if active else "#2c2c31"
        fill = "url(#cardActive)" if active else "url(#card)"
        rows = []
        for k, l in enumerate(lines):
            y = 150 + k * 30
            rows.append(
                f'<rect x="{x + 24}" y="{y - 11}" width="8" height="8" fill="{RED if active else "#5b5b62"}" transform="rotate(45 {x + 28} {y - 7})"/>'
                f'<text x="{x + 44}" y="{y - 2}" class="line">{l}</text>'
            )
        tag_svg = (f'<rect x="{x + cw - 118}" y="38" width="94" height="24" fill="none" stroke="{RED}" stroke-width="1.5"/>'
                   f'<text x="{x + cw - 71}" y="55" class="tagtxt" text-anchor="middle">{tag}</text>') if tag else ""
        out.append(f"""
  <g class="card" style="animation-delay:{d:.2f}s">
    <rect x="{x}" y="24" width="{cw:.1f}" height="{H - 48}" fill="{fill}" stroke="{border}" stroke-width="1.5"/>
    <rect x="{x}" y="24" width="{cw:.1f}" height="4" fill="{RED}" class="{'topbar' if active else 'topbar off'}"/>
    <text x="{x + 24}" y="72" class="name">{name}</text>
    {tag_svg}
    <text x="{x + 24}" y="104" class="hook">{hook}</text>
    <rect x="{x + 24}" y="122" width="{cw - 48:.1f}" height="1" fill="#2c2c31"/>
    {''.join(rows)}
  </g>""")

    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" role="img" aria-label="Roulette 47 modes">
  <title>Roulette 47 — modes</title>
  <style>
    text {{ font-family: Segoe UI, Helvetica Neue, Arial, sans-serif; }}
    .name {{ fill: {WHITE}; font-size: 30px; font-weight: 700; letter-spacing: 4px; }}
    .hook {{ fill: #a9a9b2; font-size: 17px; font-style: italic; }}
    .line {{ fill: #d0d0d6; font-size: 16px; }}
    .tagtxt {{ fill: #ff5363; font-size: 12px; font-weight: 700; letter-spacing: 2px; }}
    .card {{ opacity: 0; animation: in .7s cubic-bezier(.2,.8,.2,1) forwards; }}
    .topbar {{ transform-box: fill-box; transform-origin: left; transform: scaleX(0); animation: bar 1s 1s cubic-bezier(.2,.8,.2,1) forwards; }}
    .topbar.off {{ opacity: 0; }}
    @keyframes in {{ from {{ opacity: 0; transform: translateY(18px); }} to {{ opacity: 1; transform: translateY(0); }} }}
    @keyframes bar {{ to {{ transform: scaleX(1); }} }}
  </style>
  <defs>
    <linearGradient id="card" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#1b1b1e"/><stop offset="1" stop-color="#0d0d0f"/>
    </linearGradient>
    <linearGradient id="cardActive" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#3a0a10"/><stop offset="1" stop-color="#100909"/>
    </linearGradient>
  </defs>
  <rect width="{W}" height="{H}" fill="#08080a"/>
  {''.join(out)}
</svg>
"""


if __name__ == "__main__":
    ASSETS.mkdir(exist_ok=True)
    (ASSETS / "banner.svg").write_text(banner(), encoding="utf-8")
    (ASSETS / "modes.svg").write_text(modes(), encoding="utf-8")
    print("assets/banner.svg, assets/modes.svg written")
