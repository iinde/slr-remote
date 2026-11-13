"""data-flow: the big picture of how a press travels and how the answer returns.

Writes two versions: data-flow (the short one for the README and the landing page) and
data-flow-full (with the step captions, for docs/data-flow.md)."""
from slr import *


def build(short):
    c = Canvas(1180 if short else 1830)
    c.header("How a press reaches the camera",
             "One button in your hand, a camera somewhere far away. The press travels by LoRa "
             "(a long range radio that gets through walls and trees) to a unit sitting with the camera, which presses "
             "the shutter over Bluetooth. No wires to the camera, no phone, no app.")

    TOP, BOT = 430, (905 if short else 985)
    FWD, RET = 590, 830           # y of the outward and return rows
    CH = 62                       # chip height


    def unit(x, w, title, sub):
        c.rect(x, TOP, w, BOT - TOP, WHITE, LINE, 1.5, 18)
        c.text(x + 30, TOP + 52, title, 31, BLACK, BOLD)
        c.text(x + 30, TOP + 88, sub, 24, SOFT)


    # ---- YOU ---------------------------------------------------------------
    c.rect(M, TOP, 190, BOT - TOP, BG, LINE, 1.5, 18)
    c.text(M + 95, TOP + 52, "YOU", 31, BLACK, BOLD, ha="center")
    c.text(M + 95, TOP + 88, "one thumb", 24, SOFT, ha="center")
    c.circle(M + 95, FWD, 46, LIGHT, LINE, 1.5)
    c.circle(M + 95, FWD, 30, ORANGE, ORANGE, 0)
    c.text(M + 95, FWD + 86, "press", 24, SOFT, ha="center")
    c.text(M + 95, RET + 8, "read the", 24, SOFT, ha="center")
    c.text(M + 95, RET + 40, "screen", 24, SOFT, ha="center")

    # ---- HANDHELD ----------------------------------------------------------
    HX, HW = 360, 440
    unit(HX, HW, "HANDHELD", "in your pocket")
    cx, cw = HX + 30, HW - 60
    c.chip(cx, FWD - CH / 2, cw, CH, "Button", LIGHT, LINE)
    if short:
        c.text(HX + HW / 2, (FWD + RET) / 2 + 8, "one button, small screen", 25, SOFT, ha="center")
    else:
        c.chip(cx, FWD + 80 - CH / 2, cw, CH, "ESP32-S3 processor")
        c.chip(cx, FWD + 160 - CH / 2, cw, CH, "LoRa radio (SX1262)")
        c.chip(cx, RET + 80 - CH / 2, cw, CH, "LiPo battery")
    c.screen(cx, RET - CH / 2, cw, CH, "PRESSED", 34)

    # ---- CAMERA UNIT -------------------------------------------------------
    UX, UW = 1350, 440
    unit(UX, UW, "CAMERA UNIT", "sits with the camera")
    ux, uw = UX + 30, UW - 60
    if short:
        c.chip(ux, FWD - CH / 2, uw, CH, "Radio in, Bluetooth out")
        c.text(UX + UW / 2, (FWD + RET) / 2 + 8, "presses the shutter for you", 25, SOFT, ha="center")
        c.chip(ux, RET - CH / 2, uw, CH, "Sends the answer back")
    else:
        c.chip(ux, FWD - CH / 2, uw, CH, "LoRa radio (SX1262)")
        c.chip(ux, FWD + 80 - CH / 2, uw, CH, "ESP32-S3 processor")
        c.chip(ux, FWD + 160 - CH / 2, uw, CH, "Bluetooth LE")
        c.chip(ux, RET - CH / 2, uw, CH, "GPS (optional)", BG, GREY, SOFT, ls=(0, (5, 4)))
        c.chip(ux, RET + 80 - CH / 2, uw, CH, "Tally LED (optional)", BG, GREY, SOFT, ls=(0, (5, 4)))

    # ---- CAMERA ------------------------------------------------------------
    KX, KW = 2060, W - M - 2060
    unit(KX, KW, "CAMERA", "Fujifilm X-E5")
    kx, ky = KX + KW / 2, 700
    c.rect(kx - 92, ky - 52, 184, 118, LIGHT, GRAPHITE, 2.5, 14, 3)      # body
    c.rect(kx - 70, ky - 68, 44, 18, GRAPHITE, GRAPHITE, 0, 0, 3)        # dial
    c.rect(kx + 40, ky - 64, 30, 14, ORANGE, ORANGE, 0, 0, 3)            # shutter button
    c.circle(kx + 8, ky + 8, 40, WHITE, GRAPHITE, 2.5, 4)
    c.circle(kx + 8, ky + 8, 22, GRAPHITE, GRAPHITE, 0, 5)
    c.text(kx, ky + 118, "the camera", 24, SOFT, ha="center")
    c.text(kx, ky + 150, "shoots", 24, SOFT, ha="center")

    # ---- hop 1 and 6: you <-> handheld -------------------------------------
    c.arrow(M + 190 + 8, FWD, HX - 8, FWD, ORANGE, 3.2)
    c.badge((M + 190 + HX) / 2, FWD - 48, 1)
    c.arrow(HX - 8, RET, M + 190 + 8, RET, GRAPHITE, 2.6)
    c.badge((M + 190 + HX) / 2, RET - 48, 6, fc=GRAPHITE)

    # ---- LoRa link ---------------------------------------------------------
    LX1, LX2 = HX + HW, UX
    lm = (LX1 + LX2) / 2
    c.text(lm, TOP + 30, "LoRa RADIO LINK", 27, ORANGE, DIN, ha="center")
    c.text(lm, TOP + 70, "869.525 MHz, built for distance", 26, GRAPHITE, MED, ha="center")
    c.wave(LX1 + 10, LX2 - 10, FWD, ORANGE, 3.2)
    c.badge(lm, FWD - 56, 2)
    c.text(lm, FWD + 52, "the command, signed", 25, GRAPHITE, MED, ha="center")
    if not short:
        c.text(lm, FWD + 84, "(plus the handheld's battery level", 22, SOFT, ha="center")
        c.text(lm, FWD + 112, "on its regular check-ins)", 22, SOFT, ha="center")

    c.wave(LX1 + 10, LX2 - 10, RET, GRAPHITE, 2.6, flip=True)
    c.badge(lm, RET - 56, 5, fc=GRAPHITE)
    c.text(lm, RET + 52, "a quick Ack, then the Result", 25, GRAPHITE, MED, ha="center")
    if not short:
        c.text(lm, RET + 84, "(plus the camera unit's battery level,", 22, SOFT, ha="center")
        c.text(lm, RET + 112, "signal strength and GPS clock)", 22, SOFT, ha="center")

    # ---- Bluetooth link ----------------------------------------------------
    BX1, BX2 = UX + UW, KX
    bm = (BX1 + BX2) / 2
    c.text(bm, TOP + 30, "BLUETOOTH LE", 27, GRAPHITE, DIN, ha="center")
    c.text(bm, TOP + 70, "a few metres", 26, GRAPHITE, MED, ha="center")
    c.arrow(BX1 + 10, FWD, BX2 - 10, FWD, ORANGE, 3.2)
    c.badge(bm, FWD - 56, 3)
    c.text(bm, FWD + 52, "focus, shutter,", 25, GRAPHITE, MED, ha="center")
    c.text(bm, FWD + 84, "release", 25, GRAPHITE, MED, ha="center")
    c.arrow(BX2 - 10, RET, BX1 + 10, RET, GRAPHITE, 2.6)
    c.badge(bm, RET - 56, 4, fc=GRAPHITE)
    c.text(bm, RET + 52, "\"received\"", 25, GRAPHITE, MED, ha="center")

    # ---- legend ------------------------------------------------------------
    ly = BOT + 65
    c.line([(M, ly), (M + 70, ly)], ORANGE, 3.2, cap="butt")
    c.text(M + 86, ly + 9, "the press going out", 25, SOFT)
    c.line([(M + 400, ly), (M + 470, ly)], GRAPHITE, 2.6, cap="butt")
    c.text(M + 486, ly + 9, "the answer coming back", 25, SOFT)
    c.wave(M + 850, M + 940, ly, GREY, 3, amp=9, period=30, size=14)
    c.text(M + 956, ly + 9, "wavy = LoRa radio", 25, SOFT)
    c.arrow(M + 1250, ly, M + 1340, ly, GREY, 3, size=14)
    c.text(M + 1356, ly + 9, "straight = Bluetooth, or your thumb and eyes", 25, SOFT)

    if not short:
        # ---- captions ----------------------------------------------------------
        caps = [
            (1, "You press the button",
             "A tap takes a photo. A hold is a half-press: it keeps focus until you let go."),
            (2, "The handheld sends a command by LoRa",
             "A 25 byte radio message, signed with a key the two units agreed when they were paired, "
             "so only your handheld can fire your camera."),
            (3, "The camera unit presses the shutter",
             "It uses the camera's own Bluetooth remote release: focus press, shutter press, release."),
            (4, "The camera acknowledges",
             "It confirms each Bluetooth command as it lands: focus, shutter, release."),
            (5, "The camera unit answers by LoRa",
             "A quick Ack as soon as the command arrives, then a Result once the press is done."),
            (6, "The handheld shows PRESSED",
             "It shows for 4 seconds, then the screen goes back to READY for the next frame."),
        ]
        c.line([(M, 1110), (W - M, 1110)], LINE, 1.5)
        colw = (W - 2 * M) / 3
        for i, (n, head, body) in enumerate(caps):
            x = M + (i % 3) * colw
            y = 1180 + (i // 3) * 190
            c.badge(x + 26, y - 9, n, fc=ORANGE if n <= 3 else GRAPHITE)
            c.text(x + 70, y, head, 28, BLACK, BOLD)
            c.para(x + 70, y + 42, body, 25, colw - 120, SOFT)

        # ---- link facts ------------------------------------
        fy = 1540
        c.rect(M, fy, 1070, 132, WHITE, LINE, 1.5, 14)
        c.text(M + 30, fy + 44, "THE RADIO LINK LOOKS AFTER ITSELF", 23, ORANGE, DIN)
        c.para(M + 30, fy + 82,
               "It picks its own speed (the screen shows FAST or SLOW), and it never uses more than "
               "10% of any hour on air, the legal limit on this band. Every command is signed.",
               24, 1010, GRAPHITE, lead=1.34)
        c.rect(M + 1100, fy, W - 2 * M - 1100, 132, WHITE, LINE, 1.5, 14)
        c.text(M + 1130, fy + 44, "PRESSED MEANS THE CAMERA TOOK THE PRESS", 23, ORANGE, DIN)
        c.para(M + 1130, fy + 82,
               "The camera answers every command it is sent, and the tick on your screen is that answer, "
               "carried all the way back to your hand.",
               24, W - 2 * M - 1160, GRAPHITE, lead=1.34)

    c.footer("Two Heltec WiFi LoRa 32 V4 boards. Built for the Fujifilm X-E5.")
    c.save("data-flow" if short else "data-flow-full")


build(True)
build(False)
