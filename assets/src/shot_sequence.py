"""shot-sequence: one tap, step by step, across four lanes. Time runs downwards."""
from slr import *

c = Canvas(2380)
c.header("One press, step by step",
         "What happens between your thumb and the shutter on a single tap. "
         "Time runs down the page, with timings from my bench.")

YOU, HH, CU, CAM = 250, 680, 1260, 1700
NX = 1900                      # notes column
NW = W - M - NX
TOP = 400

# lane headers and lifelines
END = 2070
for x, name, sub in [(YOU, "YOU", ""), (HH, "HANDHELD", ""), (CU, "CAMERA UNIT", ""),
                     (CAM, "CAMERA", "")]:
    w = 250
    c.rect(x - w / 2, TOP, w, 68, GRAPHITE, GRAPHITE, 0, 12, 4)
    c.text(x, TOP + 34, name, 27, WHITE, BOLD, ha="center", va="center_baseline", z=6)
    c.line([(x, TOP + 68), (x, END)], LINE, 2.2, z=1)

c.text((HH + CU) / 2, TOP + 44, "LoRa radio", 25, ORANGE, MED, ha="center")
c.text((CU + CAM) / 2, TOP + 44, "Bluetooth", 25, SOFT, MED, ha="center")
c.text(NX, TOP + 44, "TIMING AND NOTES", 24, GREY, DIN)

G = 14  # gap between arrow ends and lifelines


def dot(x, y, col=GRAPHITE):
    c.circle(x, y, 9, col, col, 0, 5)


def msg(y, a, b, label, col=GRAPHITE, wavy=False, sub=None):
    d = 1 if b > a else -1
    dot(a, y, col)
    if wavy:
        c.wave(min(a, b) + G, max(a, b) - G, y, col, 3.0 if col == ORANGE else 2.6, flip=d < 0)
    else:
        c.arrow(a + d * G, y, b - d * G, y, col, 3.0 if col == ORANGE else 2.6)
    c.text((a + b) / 2, y - 30, label, 28, BLACK, BOLD, ha="center")
    if sub:
        c.text((a + b) / 2, y + 46, sub, 23, SOFT, ha="center")


def step(n, y):
    c.badge(M + 24, y, n, r=21, fc=LIGHT, color=GRAPHITE)


def note(y, strong, body=None):
    yy = y - 6
    if strong:
        yy = c.para(NX, yy, strong, 25, NW, BLACK, BOLD, lead=1.3)
    if body:
        c.para(NX, yy, body, 23, NW, SOFT, lead=1.32)


def lcd(y, word):
    c.screen(HH - 250, y - 30, 190, 60, word, 30)
    c.line([(HH - 60, y), (HH - 12, y)], LINE, 2.2, z=1)


def wait(y1, y2, label):
    c.rect(CU - 13, y1, 26, y2 - y1, TINT, ORANGE, 1.5, 0, 4)
    c.text(CU - 34, (y1 + y2) / 2, label, 24, GRAPHITE, MED, ha="right", va="center_baseline")


R = [560, 710, 860, 1010, 1150, 1290, 1420, 1550, 1700, 1885, 2010]

# 1 tap
step(1, R[0])
msg(R[0], YOU, HH, "Tap the button", ORANGE)
note(R[0], "A tap is under 300 ms", "Hold longer and it becomes a half-press that keeps focus.")

# 2 command out
step(2, R[1])
msg(R[1], HH, CU, "Shot command", ORANGE, wavy=True)
lcd(R[1] + 62, "SENT")
note(R[1], "62 ms on air at the fastest setting",
     "About 1.5 s at the slowest, longest range setting.")

# 3 ack back
step(3, R[2])
msg(R[2], CU, HH, "Ack: \"got it\"", GRAPHITE, wavy=True)
lcd(R[2] + 62, "AT CAM")
note(R[2], "About 180 to 225 ms",
     "Measured at the fast setting: handheld sending to camera unit accepting the press.")

# 4 focus press
step(4, R[3])
msg(R[3], CU, CAM, "Focus press", ORANGE)
note(R[3], "About 140 ms per command", "Measured. The camera acknowledges each Bluetooth command.")

# 5 wait for focus
step(5, R[4])
wait(R[4] - 50, R[4] + 50, "wait for focus")
c.text(CAM + 28, R[4] + 8, "focusing", 24, SOFT)
note(R[4], "500 ms by default", "Adjustable in the setup menu: 300, 500, 800 or 1200 ms.")

# 6 shutter press
step(6, R[5])
msg(R[5], CU, CAM, "Shutter press", ORANGE)
c.circle(CAM, R[5], 15, ORANGE, ORANGE, 0, 6)
c.text(CAM + 28, R[5] - 6, "the camera", 24, GRAPHITE, MED)
c.text(CAM + 28, R[5] + 24, "shoots", 24, GRAPHITE, MED)
note(R[5], "The camera gets the full press",
     "The camera acknowledges the press.")

# 7 hold
step(7, R[6])
wait(R[6] - 40, R[6] + 40, "hold the shutter")
note(R[6], "200 ms")

# 8 release
step(8, R[7])
msg(R[7], CU, CAM, "Release", GRAPHITE)
note(R[7], "The camera unit always lets go",
     "Even if the handheld has gone quiet.")

# 9 result back
step(9, R[8])
msg(R[8], CU, HH, "Result: press accepted", GRAPHITE, wavy=True)
lcd(R[8] + 62, "PRESSED")
note(R[8], "The answer comes back",
     "Accepted means the camera acknowledged the shutter command.")

# 10 you see it
step(10, R[9])
msg(R[9], HH, YOU, "You see PRESSED", GRAPHITE)
note(R[9], "Shown for 4 seconds")

# 11 back to ready
step(11, R[10])
lcd(R[10], "READY")
c.text(HH + 28, R[10] + 8, "ready for the next press", 24, SOFT)
note(R[10], "Then back to READY")

# footnote
fy = 2125
c.rect(M, fy, W - 2 * M, 104, WHITE, LINE, 1.5, 14)
c.text(M + 30, fy + 44, "NOT TO SCALE", 23, ORANGE, DIN)
c.para(M + 30, fy + 80,
       "Orange is the press going out, dark grey is the answer coming back, wavy lines are LoRa radio. "
       "On the slow link settings every radio step takes longer; the Bluetooth steps stay the same.",
       24, W - 2 * M - 60, GRAPHITE)

c.footer("Firmware 0.4.0, EU 868, relay mode, single shot.")
c.save("shot-sequence")
