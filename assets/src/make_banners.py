"""Compose the repo banners from the hero render.

Run:  python assets/src/make_banners.py
Writes banner.png, banner-dark.png, social-preview.png, feature-strip.png into assets/.
"""
from pathlib import Path
import math

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ASSETS = Path(__file__).resolve().parent.parent
HERO = ASSETS / "renders" / "hero.png"
if not HERO.exists():
    HERO = ASSETS / "renders" / "01_hero_both.png"

BLACK = (14, 15, 17)
GRAPHITE = (35, 39, 43)
GREY = (134, 138, 143)
LIGHT = (236, 238, 240)
ORANGE = (255, 100, 16)

AVENIR = "/System/Library/Fonts/Avenir Next.ttc"
_cache = {}


def font(style, size):
    """Avenir Next face by style name (Heavy, Bold, Demi Bold, Medium, Regular)."""
    key = (style, size)
    if key not in _cache:
        for i in range(12):
            f = ImageFont.truetype(AVENIR, size, index=i)
            if f.getname()[1] == style:
                _cache[key] = f
                break
        else:
            raise SystemExit("no Avenir Next style " + style)
    return _cache[key]


def hero_crop(pad=30):
    """Hero render cropped to its content (cases plus soft shadow)."""
    im = Image.open(HERO).convert("RGB")
    a = np.asarray(im).astype(int)
    d = np.abs(a - np.array(LIGHT)).max(axis=2)
    ys, xs = np.where(d > 2)
    box = (max(xs.min() - pad, 0), max(ys.min() - pad, 0),
           min(xs.max() + pad, im.width), min(ys.max() + pad, im.height))
    return im.crop(box)


def fit(im, w, h):
    s = min(w / im.width, h / im.height)
    return im.resize((round(im.width * s), round(im.height * s)), Image.LANCZOS)


def text(d, xy, s, f, fill, tracking=0):
    """Draw text with its cap top at xy[1]. Returns right edge x."""
    x, y = xy
    top = f.getbbox("H")[1]
    if not tracking:
        d.text((x, y - top), s, font=f, fill=fill)
        return x + d.textlength(s, font=f)
    for ch in s:
        d.text((x, y - top), ch, font=f, fill=fill)
        x += d.textlength(ch, font=f) + tracking
    return x - tracking


def cap(f):
    b = f.getbbox("H")
    return b[3] - b[1]


def banner(path, W, H, k, dark=False, margin=None, hero_w=None, title=150,
           strap=44, head=58, line=30, by=26, text_w=None, lift=0):
    """k scales type relative to the 2400 x 900 master."""
    S = 2  # supersample
    bg = BLACK if dark else LIGHT
    fg = LIGHT if dark else BLACK
    sub = (190, 194, 198) if dark else (70, 74, 79)
    img = Image.new("RGB", (W * S, H * S), bg)
    d = ImageDraw.Draw(img)
    m = (margin or round(120 * k)) * S

    hero = hero_crop()
    hw = (hero_w or round(1300 * k)) * S
    if dark:
        # the render keeps its own light ground, set as a rounded panel
        pw, ph = hw, H * S - 2 * m
        panel = Image.new("RGB", (pw, ph), LIGHT)
        hh = fit(hero, pw - 40 * S * k, ph - 40 * S * k)
        panel.paste(hh, ((pw - hh.width) // 2, (ph - hh.height) // 2))
        mask = Image.new("L", (pw, ph), 0)
        ImageDraw.Draw(mask).rounded_rectangle((0, 0, pw - 1, ph - 1), radius=round(28 * k * S), fill=255)
        img.paste(panel, (W * S - m - pw, m), mask)
    else:
        hh = fit(hero, hw, H * S - round(60 * k) * S)
        img.paste(hh, (W * S - round(40 * k) * S - hh.width, (H * S - hh.height) // 2))

    f_title = font("Heavy", round(title * k * S))
    f_strap = font("Demi Bold", round(strap * k * S))
    f_head = font("Bold", round(head * k * S))
    f_line = font("Medium", round(line * k * S))
    f_by = font("Medium", round(by * k * S))

    heads = ("A camera remote unlike any other.", "One that works for miles.")
    small = "One button. No phone. No app. For the Fujifilm X-E5."
    if text_w:  # the text column must stay clear of the render
        for s_, f_ in ((heads[0], f_head), (small, f_line), ("SLR Remote", f_title)):
            assert d.textlength(s_, font=f_) <= text_w * S, (path, s_, d.textlength(s_, font=f_) / S)

    bar_h = round(10 * k * S)
    g1, g2, g3 = round(48 * k * S), round(40 * k * S), round(64 * k * S)
    g4, g5 = round(26 * k * S), round(38 * k * S)
    block = (bar_h + g1 + cap(f_title) + g2 + cap(f_strap) + g3
             + 2 * cap(f_head) + g4 + g5 + cap(f_line))
    y = (H * S - block) // 2 - round(lift * k * S)
    d.rectangle((m, y, m + round(120 * k * S), y + bar_h), fill=ORANGE)
    y += bar_h + g1
    # optical alignment: pull the heavy S back so its stem lines up with the bar
    text(d, (m - round(title * 0.035 * k * S), y), "SLR Remote", f_title, fg, tracking=-round(3 * k * S))
    y += cap(f_title) + g2
    text(d, (m, y), "Super Long Range Remote", f_strap, ORANGE)
    y += cap(f_strap) + g3
    text(d, (m, y), heads[0], f_head, fg)
    y += cap(f_head) + g4
    text(d, (m, y), heads[1], f_head, fg)
    y += cap(f_head) + g5
    text(d, (m, y), small, f_line, sub)
    text(d, (m, H * S - round((margin or 120 * k) * 0.6) * S - cap(f_by)), "by Aaron Phull", f_by, GREY)

    img.resize((W, H), Image.LANCZOS).save(path, optimize=True)


# ---------------------------------------------------------------- icons
def icon_button(d, cx, cy, r, w):
    d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=BLACK, width=w)
    q = r * 0.42
    d.ellipse((cx - q, cy - q, cx + q, cy + q), fill=ORANGE)


def icon_range(d, cx, cy, r, w):
    # two units with radio arcs between them
    x0, x1 = cx - r, cx + r
    q = r * 0.13
    d.ellipse((x0 - q, cy - q, x0 + q, cy + q), fill=BLACK)
    d.ellipse((x1 - q, cy - q, x1 + q, cy + q), fill=ORANGE)
    for i, rr in enumerate((0.42, 0.78, 1.14)):
        a = r * rr
        d.arc((x0 - a, cy - a, x0 + a, cy + a), -38, 38, fill=BLACK, width=w)


def icon_nophone(d, cx, cy, r, w):
    pw, ph = r * 0.56, r
    d.rounded_rectangle((cx - pw, cy - ph, cx + pw, cy + ph), radius=r * 0.16, outline=BLACK, width=w)
    d.line((cx - pw * 0.3, cy + ph * 0.76, cx + pw * 0.3, cy + ph * 0.76), fill=BLACK, width=w)
    k = r * 0.98
    # knock a gap behind the slash, then the slash itself
    d.line((cx - k, cy + k * 0.8, cx + k, cy - k * 0.8), fill=LIGHT, width=w * 4)
    d.line((cx - k, cy + k * 0.8, cx + k, cy - k * 0.8), fill=ORANGE, width=w)


def icon_print(d, cx, cy, r, w):
    # nozzle above stacked layers
    nw = r * 0.34
    top = cy - r
    d.line((cx - nw, top, cx + nw, top), fill=BLACK, width=w)
    d.line((cx - nw, top, cx - nw, top + r * 0.3), fill=BLACK, width=w)
    d.line((cx + nw, top, cx + nw, top + r * 0.3), fill=BLACK, width=w)
    d.line((cx - nw, top + r * 0.3, cx, top + r * 0.68), fill=BLACK, width=w)
    d.line((cx + nw, top + r * 0.3, cx, top + r * 0.68), fill=BLACK, width=w)
    ys = [cy + r * t for t in (0.0, 0.33, 0.66, 0.99)]
    for i, y in enumerate(ys):
        half = r * (0.62 if i == 0 else 1.0)
        d.line((cx - half, y, cx + half, y), fill=ORANGE if i == 0 else BLACK, width=w)


def feature_strip(path, W=2400, H=520):
    S = 2
    img = Image.new("RGB", (W * S, H * S), LIGHT)
    d = ImageDraw.Draw(img)
    tiles = [
        (icon_button, "One button", "Tap to shoot, hold for focus"),
        (icon_range, "Works for miles", "Long-range radio, far beyond Bluetooth"),
        (icon_nophone, "No phone, no app", "Talks to the camera directly"),
        (icon_print, "Build your own", "Printed case, open firmware"),
    ]
    tw = W * S / 4
    f1 = font("Bold", 42 * S)
    f2 = font("Medium", 28 * S)
    for i, (icon, a, b) in enumerate(tiles):
        cx = tw * i + tw / 2
        if i:
            d.line((tw * i, 110 * S, tw * i, (H - 110) * S), fill=(205, 208, 212), width=S * 2)
        icon(d, cx, 168 * S, 62 * S, 7 * S)
        for s, f, fill, y in ((a, f1, BLACK, 312), (b, f2, (88, 92, 97), 386)):
            text(d, (cx - d.textlength(s, font=f) / 2, y * S), s, f, fill)
    img.resize((W, H), Image.LANCZOS).save(path, optimize=True)


if __name__ == "__main__":
    banner(ASSETS / "banner.png", 2400, 900, 1.0, head=54, hero_w=1270, text_w=1000, lift=30)
    banner(ASSETS / "banner-dark.png", 2400, 900, 1.0, dark=True, margin=100, hero_w=1100,
           text_w=1020, lift=30)
    banner(ASSETS / "social-preview.png", 1280, 640, 0.62, margin=56, hero_w=590, title=150,
           strap=50, head=62, line=35, by=30, text_w=640, lift=30)
    feature_strip(ASSETS / "feature-strip.png")
    print("done")
