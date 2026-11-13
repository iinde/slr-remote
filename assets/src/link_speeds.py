"""link-speeds: the six LoRa settings, time on air and check-in interval.

Figures are calculated from the radio settings the firmware uses
(125 kHz bandwidth, coding rate 4/5, 8 symbol preamble, explicit header, CRC on).
"""
from slr import *

SF = [7, 8, 9, 10, 11, 12]
CMD = [62, 114, 206, 412, 824, 1483]          # ms, 25 byte Shot command
REPLY = [93, 175, 309, 576, 1233, 2302]       # ms, 47 byte Ack or Result
PING = [3.10, 5.78, 10.30, 18.94, 41.14, 75.70]  # s between check-ins

c = Canvas(1700)
c.header("Fast or far: the six link speeds",
         "LoRa has six speed settings, called spreading factors (SF7 to SF12). The remote picks one by itself "
         "and shows FAST or SLOW on screen. Slower settings reach further.")

R0, RP = 560, 128              # first row centre, row pitch
LX = M                         # label column
AX, AW = 440, 820              # panel A bar origin and full-scale width
BX, BW = 1520, 600             # panel B

# bands
for i0, word, fc in [(0, "FAST", TINT), (3, "SLOW", LIGHT)]:
    y = R0 + i0 * RP - RP / 2 + 6
    c.rect(LX, y, W - 2 * M, 3 * RP - 12, fc, fc, 0, 16, 0, alpha=0.75 if fc == TINT else 1)
    c.screen(LX + 28, y + (3 * RP - 12) / 2 - 38, 170, 76, word, 40)

# column heads
hy = 440
c.text(LX + 240, hy, "SETTING", 23, GREY, DIN)
c.text(AX, hy, "TIME ON AIR FOR ONE MESSAGE", 23, GREY, DIN)
c.rect(AX + 420, hy - 20, 26, 20, ORANGE, ORANGE, 0)
c.text(AX + 456, hy, "your command", 23, SOFT)
c.rect(AX + 640, hy - 20, 26, 20, GREY, GREY, 0)
c.text(AX + 676, hy, "the reply", 23, SOFT)
c.text(BX, hy, "HOW OFTEN THE UNITS CHECK IN", 23, GREY, DIN)

amax, bmax = 2302.0, 75.70


def ms(v):
    return f"{v} ms" if v < 1000 else f"{v / 1000:.1f} s"


for i, sf in enumerate(SF):
    y = R0 + i * RP
    c.text(LX + 240, y + 13, f"SF{sf}", 40, BLACK, BOLD)
    # command and reply bars
    w1 = AW * CMD[i] / amax
    w2 = AW * REPLY[i] / amax
    c.rect(AX, y - 30, w1, 26, ORANGE, ORANGE, 0, 0, 3)
    c.text(AX + w1 + 14, y - 8, ms(CMD[i]), 27, BLACK, BOLD)
    c.rect(AX, y + 4, w2, 26, GREY, GREY, 0, 0, 3)
    c.text(AX + w2 + 14, y + 26, ms(REPLY[i]), 25, SOFT, MED)
    # check-in interval
    w3 = BW * PING[i] / bmax
    c.rect(BX, y - 13, w3, 26, GRAPHITE, GRAPHITE, 0, 0, 3)
    c.text(BX + w3 + 14, y + 10, f"every {PING[i]:.0f} s" if PING[i] >= 10 else f"every {PING[i]:.1f} s",
           27, BLACK, BOLD)

# axis base lines
yb1, yb2 = R0 - RP / 2 + 14, R0 + 5 * RP + RP / 2 - 14
c.line([(AX, yb1), (AX, yb2)], GRAPHITE, 1.6, z=4)
c.line([(BX, yb1), (BX, yb2)], GRAPHITE, 1.6, z=4)

# notes
ny = R0 + 5 * RP + RP / 2 + 70
colw = (W - 2 * M - 2 * 50) / 3
notes = [
    ("Slower reaches further",
     "Each step up roughly doubles the time on air. In return the receiver can pick out a weaker "
     "signal, so the link gets through at longer distances and past more obstacles."),
    ("It chooses for you",
     "The handheld checks the signal on every check-in and moves up or down a setting by itself. "
     "You can also fix one setting in the setup menu."),
    ("It stays legal",
     "On this band each unit may transmit for 10% of any hour (360 seconds). The firmware counts "
     "every message, keeps count across restarts, and refuses to go over."),
]
for i, (h, b) in enumerate(notes):
    x = M + i * (colw + 50)
    c.rect(x, ny - 6, 44, 6, ORANGE, ORANGE, 0)
    c.text(x, ny + 48, h, 31, BLACK, BOLD)
    c.para(x, ny + 94, b, 25, colw, SOFT)

c.footer("Airtime worked out from the radio settings (869.525 MHz, 125 kHz bandwidth).")
c.save("link-speeds")
