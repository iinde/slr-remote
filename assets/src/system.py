"""system: parts overview of the two units. Outlines are to scale, internals simplified."""
from slr import *

c = Canvas(1670)
c.header("Two units, one firmware",
         "A pocket handheld with one button, and a camera unit that sits with the camera. "
         "Both are the same Heltec WiFi LoRa 32 V4 board running the same firmware, with the role chosen in setup.")

S = 7.6                        # pixels per millimetre
PT, PB = 400, 1500             # panel top and bottom
CY = 842                       # y of the centre line of both cases
PW = (W - 2 * M - 40) / 2
LEN = 93.0                     # mm from the USB end to the antenna tip


def body(cx, gps):
    """Seen from above as it sits on the desk: USB end on the left, antenna on the right, screen upright.
    L runs along the case from the USB end, A across it (minus is up the page)."""
    X = lambda l: cx - LEN * S / 2 + l * S
    Y = lambda a: CY + a * S
    r = 6.35 * S
    parts = [(-22.86, 0, 22.86, 60.96)]
    if gps:
        parts.append((2.54, 30, 22.86, 81.28))
    for ec, lw, z in [(GRAPHITE, 5, 2), (LIGHT, 0, 3)]:      # outline pass, then fill pass
        for a1, l1, a2, l2 in parts:
            c.rect(X(l1), Y(a1), (l2 - l1) * S, (a2 - a1) * S, LIGHT, ec, lw, r, z)
    # antenna on its SMA connector
    ay = Y(-11.43)
    c.rect(X(60.8), ay - 3.2 * S, 5.2 * S, 6.4 * S, GREY, GREY, 0, 0, 1)
    c.rect(X(65), ay - 4.6 * S, 28 * S, 9.2 * S, GRAPHITE, GRAPHITE, 0, 4.0 * S, 1)
    # USB-C opening on the end face
    c.rect(X(0) - 9, Y(-4.5), 12, 9 * S, GRAPHITE, GRAPHITE, 0, 0, 4)
    # screws
    for sa in (-19.8, 19.8):
        for sl in (11.5, 44.5):
            c.circle(X(sl), Y(sa), 1.75 * S, GRAPHITE, GRAPHITE, 0, 4)
    # screen
    c.rect(X(17), Y(-7.6), 26.5 * S, 15.2 * S, BLACK, BLACK, 0, 1.6 * S, 4)
    c.text(X(30.25), Y(1.2), "READY", 44, LIGHT, DIN, ha="center", va="center", z=6)
    c.text(X(30.25), Y(-4.9), "FAST", 19, GREY, DIN, ha="center", va="center", z=6)
    # buttons
    c.circle(X(5.65), Y(-8.25), 3.5 * S, WHITE, LINE, 1.5, 4)
    c.circle(X(5.65), Y(-8.25), 2.45 * S, ORANGE, ORANGE, 0, 5)
    c.circle(X(5.65), Y(8.25), 3.5 * S, WHITE, LINE, 1.5, 4)
    c.circle(X(5.65), Y(8.25), 2.3 * S, GREY, GREY, 0, 5)
    if gps:
        c.rect(X(54), Y(4.7), 21.14 * S, 16 * S, "none", GREY, 1.6, 8, 5, ls=(0, (5, 4)))
        c.text(X(64.6), Y(12.7) + 9, "GPS", 26, SOFT, DIN, ha="center", z=6)
    else:
        c.circle(X(2.6), Y(21.3), 0.9 * S, GRAPHITE, GRAPHITE, 0, 5)
    c.text(X(0) - 26, CY + 9, "USB-C", 26, BLACK, BOLD, ha="right")
    return X, Y


def label(lx, top, tx, ty, name, sub):
    """A two-line label centred on lx, above the case (top) or below it, with a leader to (tx, ty)."""
    if top:
        y = CY - 268
        c.line([(lx, y + 46), (tx, ty)], GREY, 1.4, z=7)
    else:
        y = CY + 262
        c.line([(lx, y - 34), (tx, ty)], GREY, 1.4, z=7)
    c.circle(tx, ty, 6, WHITE, GRAPHITE, 1.6, 8)
    c.text(lx, y, name, 26, BLACK, BOLD, ha="center")
    c.text(lx, y + 31, sub, 23, SOFT, ha="center")


def inside(px, items):
    """The 'inside the case' block: the board with what is on it, then the extras."""
    y = 1200
    c.text(px + 36, y, "INSIDE THE CASE", 23, ORANGE, DIN)
    bw = PW - 72
    c.rect(px + 36, y + 22, bw, 150, BG, LINE, 1.5, 12)
    c.text(px + 56, y + 62, "Heltec WiFi LoRa 32 V4 board", 26, BLACK, BOLD)
    on = ["ESP32-S3 processor", "SX1262 LoRa radio", "Bluetooth LE"]
    cw = (bw - 40 - 2 * 14) / 3
    for i, t in enumerate(on):
        c.chip(px + 56 + i * (cw + 14), y + 88, cw, 62, t, WHITE, LINE, GRAPHITE, 24)
    n = len(items)
    iw = (bw - (n - 1) * 14) / n
    for i, (t, opt) in enumerate(items):
        if opt:
            c.chip(px + 36 + i * (iw + 14), y + 190, iw, 62, t, BG, GREY, SOFT, 24, ls=(0, (5, 4)))
        else:
            c.chip(px + 36 + i * (iw + 14), y + 190, iw, 62, t, WHITE, LINE, GRAPHITE, 24)


def panel(px, title, role, dims):
    c.rect(px, PT, PW, PB - PT, WHITE, LINE, 1.5, 18, 0)
    c.text(px + 36, PT + 62, title, 36, BLACK, BOLD)
    c.text(px + 36, PT + 102, role, 25, SOFT)
    c.text(px + PW - 36, PT + 62, dims, 27, GRAPHITE, DIN, ha="right")
    c.text(px + PW - 36, PT + 100, "without antenna", 23, SOFT, ha="right")


# ---- handheld ----------------------------------------------------------
p1 = M
panel(p1, "HANDHELD", "goes in your pocket, sleeps after 60 seconds idle", "61 x 45.7 x 21.6 mm")
cx = p1 + PW / 2
X, Y = body(cx, False)
label(X(5.65) - 30, True, X(5.65), Y(-8.25), "Shoot button", "tap, or hold for focus")
label(X(44.5), True, X(44.5), Y(-19.8), "M2 x 16 screws", "four per unit")
label(X(80), True, X(80), Y(-11.43), "LoRa antenna", "868 or 915 MHz, on SMA")
label(X(2.6) - 60, False, X(2.6), Y(21.3), "Lanyard hole", "inset in the corner")
label(X(26), False, X(5.65), Y(8.25), "Reset button", "recessed")
label(X(58), False, X(38), Y(5), "OLED screen", "0.96 inch, 128 x 64")
inside(p1, [("LiPo cell (LP603449)", False)])

# ---- camera unit -------------------------------------------------------
p2 = M + PW + 40
panel(p2, "CAMERA UNIT", "sits with the camera, talks to it over Bluetooth LE", "81.3 x 45.7 x 21.6 mm")
cx = p2 + PW / 2
X, Y = body(cx, True)
label(X(5.65) - 30, True, X(5.65), Y(-8.25), "Button", "next page, hold for off")
label(X(44.5), True, X(44.5), Y(-19.8), "M2 x 16 screws", "four per unit")
label(X(80), True, X(80), Y(-11.43), "LoRa antenna", "868 or 915 MHz, on SMA")
label(X(2), False, X(5.65), Y(8.25), "Reset button", "recessed")
label(X(34), False, X(34), Y(5), "OLED screen", "off after 10 s idle")
label(X(68), False, X(68), Y(18), "GPS bay", "L76K module, optional")
inside(p2, [("LiPo cell (LP603449)", False), ("L76K GPS (optional)", True), ("Tally LED (optional)", True)])

c.footer("Case outlines to scale. Screen, buttons and antenna simplified.")
c.save("system")
