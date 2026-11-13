"""Shared drawing kit for the SLR Remote diagrams.

Everything is laid out in pixels on a 2400 px wide canvas (y runs downwards).
Each diagram script builds a Canvas, draws, then calls save() to write a PNG
and an SVG (text kept as text) into assets/diagrams.
"""
import math
import os

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.font_manager import FontProperties
from matplotlib.patches import Circle, FancyBboxPatch, Polygon, Rectangle
from matplotlib.textpath import TextPath

matplotlib.rcParams["svg.fonttype"] = "none"
matplotlib.rcParams["svg.hashsalt"] = "slr-remote"

# Palette
BLACK = "#0e0f11"
GRAPHITE = "#23272b"
GREY = "#868a8f"
LIGHT = "#eceef0"
ORANGE = "#ff6410"
BG = "#f6f7f8"
WHITE = "#ffffff"
LINE = "#d3d6da"      # hairlines on the light background
SOFT = "#5b6066"      # secondary text, darker than mid grey for legibility
TINT = "#ffe9dc"      # very light orange for bands

REG = "Avenir Next"
MED = "Avenir Next Medium"
BOLD = "Avenir Next Demi Bold"
DIN = "DIN Alternate"
MONO = "Menlo"

DPI = 200
W = 2400
M = 90  # outer margin

OUT = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "diagrams"))


def pt(px):
    return px * 72.0 / DPI


def text_width(s, size, font=REG):
    """Width of a string in canvas pixels."""
    tp = TextPath((0, 0), s, size=pt(size), prop=FontProperties(family=font))
    return tp.get_extents().width * DPI / 72.0 if s.strip() else 0.0


def wrap(s, size, width, font=REG):
    out = []
    for para in s.split("\n"):
        line = ""
        for word in para.split(" "):
            trial = (line + " " + word).strip()
            if line and text_width(trial, size, font) > width:
                out.append(line)
                line = word
            else:
                line = trial
        out.append(line)
    return out


class Canvas:
    def __init__(self, height):
        self.h = height
        self.fig = plt.figure(figsize=(W / DPI, height / DPI), dpi=DPI)
        self.fig.patch.set_facecolor(BG)
        self.ax = self.fig.add_axes([0, 0, 1, 1])
        self.ax.set_xlim(0, W)
        self.ax.set_ylim(height, 0)
        self.ax.axis("off")

    # ---- primitives -------------------------------------------------
    def text(self, x, y, s, size=30, color=GRAPHITE, font=REG, ha="left",
             va="baseline", rot=0, z=5, spacing=None):
        kw = {}
        if spacing:
            kw["linespacing"] = spacing
        return self.ax.text(x, y, s, fontsize=pt(size), color=color,
                            fontfamily=font, ha=ha, va=va, rotation=rot,
                            zorder=z, **kw)

    def para(self, x, y, s, size=30, width=600, color=GRAPHITE, font=REG,
             lead=1.38, ha="left", z=5):
        """Wrapped paragraph. y is the first baseline. Returns the next baseline."""
        for line in wrap(s, size, width, font):
            self.text(x, y, line, size, color, font, ha=ha, z=z)
            y += size * lead
        return y

    def rect(self, x, y, w, h, fc=WHITE, ec=LINE, lw=1.5, r=0, z=2, ls="-",
             alpha=None):
        if r:
            p = FancyBboxPatch((x + r, y + r), w - 2 * r, h - 2 * r,
                               boxstyle=f"round,pad={r},rounding_size={r}",
                               fc=fc, ec=ec, lw=lw, zorder=z, ls=ls,
                               alpha=alpha)
        else:
            p = Rectangle((x, y), w, h, fc=fc, ec=ec, lw=lw, zorder=z, ls=ls,
                          alpha=alpha)
        self.ax.add_patch(p)
        return p

    def circle(self, x, y, r, fc=WHITE, ec=GRAPHITE, lw=1.5, z=3):
        self.ax.add_patch(Circle((x, y), r, fc=fc, ec=ec, lw=lw, zorder=z))

    def line(self, pts, color=GRAPHITE, lw=2, ls="-", z=3, cap="round"):
        xs, ys = zip(*pts)
        self.ax.plot(xs, ys, color=color, lw=lw, ls=ls, zorder=z,
                     solid_capstyle=cap, dash_capstyle="butt",
                     solid_joinstyle="round")

    def head(self, x, y, ang, color=GRAPHITE, size=20, z=4):
        """Solid arrow head with its tip at x,y pointing along ang (radians)."""
        a, b = ang + math.radians(155), ang - math.radians(155)
        pts = [(x, y), (x + size * math.cos(a), y + size * math.sin(a)),
               (x + size * math.cos(b), y + size * math.sin(b))]
        self.ax.add_patch(Polygon(pts, closed=True, fc=color, ec=color,
                                  lw=1, zorder=z, joinstyle="miter"))

    def arrow(self, x1, y1, x2, y2, color=GRAPHITE, lw=3, ls="-", size=20, z=3):
        ang = math.atan2(y2 - y1, x2 - x1)
        bx, by = x2 - 0.8 * size * math.cos(ang), y2 - 0.8 * size * math.sin(ang)
        self.line([(x1, y1), (bx, by)], color, lw, ls, z, cap="butt")
        self.head(x2, y2, ang, color, size, z + 1)

    def wave(self, x1, x2, y, color=ORANGE, lw=3, amp=14, period=44, ls="-",
             size=20, z=3, flip=False):
        """Horizontal radio wave with an arrow head. flip draws right to left."""
        n = max(2, int(abs(x2 - x1) // 2))
        a, b = (x2, x1) if flip else (x1, x2)
        d = 1 if b > a else -1
        run = abs(b - a) - size * 1.6
        pts = []
        for i in range(n + 1):
            t = run * i / n
            # fade the wave in and out so it meets the ends on the centre line
            env = min(1.0, t / 30.0, (run - t) / 30.0)
            pts.append((a + d * t, y + amp * env * math.sin(2 * math.pi * t / period)))
        pts.append((b - d * size * 0.8, y))
        self.line(pts, color, lw, ls, z, cap="butt")
        self.head(b, y, 0 if d > 0 else math.pi, color, size, z + 1)

    def badge(self, x, y, n, r=24, fc=ORANGE, color=WHITE, z=7):
        self.circle(x, y, r, fc=fc, ec=fc, lw=0, z=z)
        self.text(x, y + r * 0.02, str(n), r * 1.15, color, BOLD, ha="center",
                  va="center_baseline", z=z + 1)

    def chip(self, x, y, w, h, label, fc=WHITE, ec=LINE, color=GRAPHITE,
             size=27, font=MED, r=10, lw=1.5, z=3, ls="-"):
        self.rect(x, y, w, h, fc, ec, lw, r, z, ls)
        self.text(x + w / 2, y + h / 2, label, size, color, font, ha="center",
                  va="center_baseline", z=z + 1)

    def screen(self, x, y, w, h, word, size=30, sub=None):
        """A little OLED: black panel, light text."""
        self.rect(x, y, w, h, BLACK, BLACK, 0, 8, 4)
        if sub:
            self.text(x + w / 2, y + h * 0.42, word, size, LIGHT, DIN,
                      ha="center", va="center_baseline", z=6)
            self.text(x + w / 2, y + h * 0.78, sub, size * 0.55, GREY, DIN,
                      ha="center", va="center_baseline", z=6)
        else:
            self.text(x + w / 2, y + h / 2, word, size, LIGHT, DIN, ha="center",
                      va="center_baseline", z=6)

    # ---- page furniture ----------------------------------------------
    def header(self, title, subtitle, kicker="SLR REMOTE"):
        self.rect(M, 92, 56, 8, ORANGE, ORANGE, 0)
        self.text(M + 76, 105, kicker, 27, GRAPHITE, DIN)
        self.text(M + 76 + text_width(kicker, 27, DIN) + 22, 105,
                  "SUPER LONG RANGE REMOTE", 27, GREY, DIN)
        self.text(M - 3, 205, title, 78, BLACK, BOLD)
        y = self.para(M, 268, subtitle, 33, W - 2 * M - 200, SOFT)
        return y

    def footer(self, note=None):
        y = self.h - 62
        self.line([(M, y - 44), (W - M, y - 44)], LINE, 1.5)
        self.text(M, y, "SLR Remote", 25, GRAPHITE, BOLD)
        self.text(M + text_width("SLR Remote", 25, BOLD) + 18, y,
                  "by Aaron Phull", 25, SOFT)
        if note:
            self.text(W - M, y, note, 25, SOFT, ha="right")

    def save(self, name):
        os.makedirs(OUT, exist_ok=True)
        png = os.path.join(OUT, name + ".png")
        svg = os.path.join(OUT, name + ".svg")
        self.fig.savefig(png, dpi=DPI, facecolor=BG)
        self.fig.savefig(svg, facecolor=BG, metadata={"Date": None,
                         "Creator": "Aaron Phull", "Title": "SLR Remote: " + name})
        plt.close(self.fig)
        print(png)
        print(svg)
