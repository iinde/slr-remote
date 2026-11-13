"""wiring: what plugs into what on each unit. A schematic, not a pin-position drawing."""
from slr import *

c = Canvas(1850)
c.header("What plugs into what",
         "Each unit is one Heltec WiFi LoRa 32 V4 board with a battery and an antenna. The camera unit "
         "can also carry a GPS module and a tally LED (a small light that shows when a shot fires).")

PT, PB = 400, 1420
PW = (W - 2 * M - 40) / 2
BT, BB, BH = 760, 1290, 150     # board top, bottom, half width
WIRE = 3.0


def pad(x, y, w=22, h=44):
    c.rect(x - w / 2, y - h / 2, w, h, GRAPHITE, GRAPHITE, 0, 0, 6)


def panel(px, title, role):
    c.rect(px, PT, PW, PB - PT, WHITE, LINE, 1.5, 18, 0)
    c.text(px + 36, PT + 62, title, 36, BLACK, BOLD)
    c.text(px + 36, PT + 102, role, 25, SOFT)
    cx = px + PW / 2
    # board
    c.rect(cx - BH, BT, 2 * BH, BB - BT, LIGHT, GRAPHITE, 2.5, 22, 3)
    c.text(cx, BT + 50, "HELTEC V4 BOARD", 23, GRAPHITE, DIN, ha="center", z=6)
    c.screen(cx - 105, BT + 80, 210, 100, "READY", 36)
    c.circle(cx - 62, BT + 270, 34, WHITE, LINE, 1.5, 4)
    c.circle(cx - 62, BT + 270, 23, ORANGE, ORANGE, 0, 5)
    c.text(cx - 62, BT + 335, "PRG", 23, GRAPHITE, MED, ha="center", z=6)
    c.circle(cx + 62, BT + 270, 34, WHITE, LINE, 1.5, 4)
    c.circle(cx + 62, BT + 270, 21, GREY, GREY, 0, 5)
    c.text(cx + 62, BT + 335, "RST", 23, GRAPHITE, MED, ha="center", z=6)
    # antenna
    ax = cx - 80
    pad(ax, BT, 44, 22)
    c.line([(ax, BT - 11), (ax, 705)], GRAPHITE, WIRE, z=2, cap="butt")
    c.rect(ax - 17, 678, 34, 30, GREY, GREY, 0, 0, 3)
    c.rect(ax - 24, 548, 48, 132, GRAPHITE, GRAPHITE, 0, 22, 3)
    c.text(ax - 52, 606, "868 MHz antenna", 26, BLACK, BOLD, ha="right")
    c.text(ax - 52, 638, "on the case's SMA connector,", 23, SOFT, ha="right")
    c.text(ax - 52, 667, "short SMA lead to the board", 23, SOFT, ha="right")
    # battery
    by = 1200
    pad(cx - BH, by, 22, 64)
    c.rect(cx - 490, by - 62, 250, 124, WHITE, GRAPHITE, 2.5, 14, 3)
    c.text(cx - 365, by - 4, "LiPo cell", 26, BLACK, BOLD, ha="center", z=6)
    c.text(cx - 365, by + 30, "LP603449", 23, SOFT, ha="center", z=6)
    c.line([(cx - 240, by - 15), (cx - BH - 11, by - 15)], ORANGE, WIRE, z=2, cap="butt")
    c.line([(cx - 240, by + 15), (cx - BH - 11, by + 15)], GRAPHITE, WIRE, z=2, cap="butt")
    c.text(cx - 365, by + 108, "to the board's battery", 23, SOFT, ha="center")
    c.text(cx - 365, by + 137, "connector (SH1.25 2-pin)", 23, SOFT, ha="center")
    # usb
    c.rect(cx - 46, BB - 9, 92, 18, GRAPHITE, GRAPHITE, 0, 0, 6)
    c.text(cx, BB + 56, "USB-C", 26, BLACK, BOLD, ha="center")
    c.text(cx, BB + 87, "firmware and console", 23, SOFT, ha="center")
    return cx


DASH = (0, (5, 4))

# ---- handheld ----------------------------------------------------------
cx = panel(M, "HANDHELD", "battery and antenna, nothing else to wire")
x0 = cx + BH
y1, y2, sx = 900, 990, cx + 240
pad(x0, y1); pad(x0, y2)
c.line([(x0 + 11, y1), (sx, y1), (sx, y1 + 26)], GREY, WIRE, z=2, cap="butt")
c.line([(x0 + 11, y2), (sx, y2), (sx, y2 - 26)], GREY, WIRE, z=2, cap="butt")
c.line([(sx, y2 - 26), (sx + 22, y1 + 30)], GRAPHITE, WIRE, z=3)          # switch lever, open
c.circle(sx, y1 + 26, 6, WHITE, GRAPHITE, 2, 4)
c.circle(sx, y2 - 26, 6, WHITE, GRAPHITE, 2, 4)
c.text(x0 + 20, y1 - 16, "GPIO6", 23, GRAPHITE, MED)
c.text(x0 + 20, y2 + 36, "GND", 23, GRAPHITE, MED)
c.text(sx + 40, y1 + 22, "Second button", 26, BLACK, BOLD)
c.text(sx + 40, y1 + 54, "optional: a switch from", 23, SOFT)
c.text(sx + 40, y1 + 83, "GPIO6 to ground", 23, SOFT)
c.line([(cx - 62, BT + 270 - 40), (cx - 62, BT + 205), (cx - 250, BT + 205)], GREY, 1.4, z=7)
c.circle(cx - 62, BT + 270, 6, WHITE, GRAPHITE, 1.6, 8)
c.text(cx - 264, BT + 200, "The shoot button", 26, BLACK, BOLD, ha="right")
c.text(cx - 264, BT + 231, "is the board's own PRG", 23, SOFT, ha="right")
c.text(cx - 264, BT + 260, "button (GPIO0)", 23, SOFT, ha="right")

# ---- camera unit -------------------------------------------------------
cx = panel(M + PW + 40, "CAMERA UNIT", "nothing plugs into the camera: that hop is Bluetooth")
x0 = cx + BH
# GPS
gy = 860
pad(x0, gy, 22, 70)
c.line([(x0 + 11, gy), (cx + 235, gy)], GREY, 7, z=2, cap="butt")
c.rect(cx + 235, gy - 62, 270, 124, BG, GREY, 1.6, 14, 3, ls=DASH)
c.text(cx + 370, gy - 4, "L76K GPS module", 26, BLACK, BOLD, ha="center", z=6)
c.text(cx + 370, gy + 30, "optional", 23, SOFT, ha="center", z=6)
c.text(cx + 235, gy + 104, "plugs into the board's", 23, SOFT)
c.text(cx + 235, gy + 133, "GNSS socket (8-pin)", 23, SOFT)
# tally LED across GPIO3 and GPIO4
y1, y2, lx = 1090, 1190, cx + 330
pad(x0, y1); pad(x0, y2)
c.line([(x0 + 11, y1), (lx, y1), (lx, y1 + 28)], ORANGE, WIRE, z=2, cap="butt")
c.line([(x0 + 11, y2), (lx, y2), (lx, y2 - 28)], GRAPHITE, WIRE, z=2, cap="butt")
c.ax.add_patch(Polygon([(lx - 24, y1 + 28), (lx + 24, y1 + 28), (lx, y2 - 30)], closed=True,
                       fc=ORANGE, ec=ORANGE, lw=1, zorder=4))
c.line([(lx - 24, y2 - 28), (lx + 24, y2 - 28)], GRAPHITE, 3.4, z=4, cap="butt")
for k in (0, 1):                                                        # light rays
    c.arrow(lx + 34 + k * 0, y1 + 44 + k * 22, lx + 70, y1 + 26 + k * 22, ORANGE, 2, size=11)
c.rect(x0 + 50, y1 - 12, 60, 24, WHITE, GRAPHITE, 2.5, 4, 5)             # series resistor
c.text(x0 + 80, y1 + 42, "220 to 330", 21, SOFT, ha="center")
c.text(x0 + 80, y1 + 67, "ohm resistor", 21, SOFT, ha="center")
c.text(x0 + 20, y1 - 22, "GPIO3  (+)", 23, GRAPHITE, MED)
c.text(x0 + 20, y2 + 36, "GPIO4  (held at 0 V)", 23, GRAPHITE, MED)
c.text(lx + 90, y1 + 36, "Tally LED", 26, BLACK, BOLD)
c.text(lx + 90, y1 + 68, "optional", 23, SOFT)

# ---- notes -------------------------------------------------------------
ny = PB + 80
colw = (W - 2 * M - 2 * 50) / 3
notes = [
    ("Antenna on first",
     "Screw the antenna on before you power a board: transmitting without one can kill the "
     "amplifier. The firmware helps by staying off the air for 2 seconds after power-on and "
     "showing ANTENNA ON?"),
    ("Tally LED on two pins",
     "Positive leg through a 220 to 330 ohm resistor to GPIO3, the other leg to GPIO4, which "
     "the firmware holds at 0 V as ground. Pick GPIO3+4 under Tally LED in the setup menu."),
    ("Or use one pin",
     "The tally LED can also run from GPIO3, GPIO4 or GPIO6 alone, with a 220 to 330 ohm "
     "resistor to ground. Check the pin labels printed on your board before soldering."),
]
for i, (h, b) in enumerate(notes):
    x = M + i * (colw + 50)
    c.rect(x, ny - 6, 44, 6, ORANGE, ORANGE, 0)
    c.text(x, ny + 48, h, 31, BLACK, BOLD)
    c.para(x, ny + 94, b, 25, colw, SOFT)

c.footer("The GPS module talks to the board on GPIO38 and GPIO39. The firmware sets all of this up.")
c.save("wiring")
