"""Turns the 128x64 frames in frames/ into bezelled PNGs and the two captioned sheets.

Run:  python make_screens.py
The frames come from build.sh, which runs the firmware's own drawing code on a PC.
"""
import os
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FRAMES = os.path.join(HERE, "frames")
OUT = os.path.normpath(os.path.join(HERE, "..", "..", "screens"))

BLACK, GRAPHITE, GREY, LIGHT, ORANGE = "#0e0f11", "#23272b", "#868a8f", "#eceef0", "#ff6410"
PANEL = "#050607"      # unlit OLED
HN = "/System/Library/Fonts/HelveticaNeue.ttc"

def font(size, bold=False):
    return ImageFont.truetype(HN, size, index=1 if bold else 0)

# file name, short caption, longer caption (README), on the short sheet
SCREENS = [
    ("splash", "Splash", "Start-up splash, shown for about 1.7 s on a cold boot only.", True),
    ("ready", "Ready", "Home screen in SHOT mode: link up, camera ready. Top bar: mode, signal bars, link speed, handheld (H) and camera unit (C) batteries.", True),
    ("sending", "Sent", "A press has gone out over LoRa. First of four progress dots filled.", True),
    ("at-cam", "At the camera unit", "The camera unit has received the press (second dot).", True),
    ("pressed", "Pressed", "The camera took the press over Bluetooth (third dot). This is the tick to look for.", True),
    ("af-hold", "Focus held", "Button held: half-press, focus held. Let go and it shoots.", True),
    ("af-lock", "Focus locked", "AF LOCK mode: a tap has locked focus for 30 s. The next tap fires at once, a hold unlocks.", True),
    ("timer", "Self-timer", "Self-timer (set to 5 s here) counting down with focus held (AF ON). Any press stops it.", True),
    ("interval", "Interval", "Interval run: shots taken, time to the next one, and the airtime left this hour.", True),
    ("video", "Video", "VIDEO mode: a tap starts recording, the next tap stops it.", True),
    ("gps", "Where the camera is", "GPS view with both boards holding a fix: distance and compass direction to the camera unit.", True),
    ("camera-unit", "Camera unit", "The camera unit's own status page: Bluetooth state, camera name, radio signal, GPS and battery.", True),
    ("slow-link", "Long-range link", "Same home screen on the long-range setting: SLOW in the top bar (spreading factor 10 to 12).", False),
    ("mode-card", "Mode card", "The mode card: tap moves down the list, hold picks. It closes by itself after 6 s.", False),
    ("link", "Link view", "LINK view: signal each way, margin, spreading factor and bandwidth, transmit power, success rate, round trip time and airtime used.", False),
    ("menu", "Setup menu", "Setup menu on a relay handheld: click moves on, hold 1 s changes the item.", False),
    ("checking", "Checking the link", "Just after power-on: the handheld has asked the camera unit and is waiting for its first reply (radar sweep).", False),
    ("unknown", "Unknown result", "The press reached the camera unit but no result came back. Shown as UNKNOWN until the next press.", False),
]

def frame(name):
    return Image.open(os.path.join(FRAMES, name + ".pgm")).convert("L")

def bezel(name, scale, pad, radius):
    """The frame scaled with nearest-neighbour, on a dark rounded bezel. Returns RGBA."""
    src = frame(name).resize((128 * scale, 64 * scale), Image.NEAREST)
    lit = Image.new("RGB", src.size, LIGHT)
    panel = Image.new("RGB", src.size, PANEL)
    panel.paste(lit, (0, 0), src)
    w, h = src.size[0] + 2 * pad, src.size[1] + 2 * pad
    ss = 4
    big = Image.new("RGBA", (w * ss, h * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    d.rounded_rectangle([0, 0, w * ss - 1, h * ss - 1], radius * ss, fill=GRAPHITE)
    b = max(2, scale // 2) * ss
    d.rounded_rectangle([b, b, w * ss - 1 - b, h * ss - 1 - b], (radius * ss) - b, fill=PANEL)
    out = big.resize((w, h), Image.LANCZOS)
    out.paste(panel, (pad, pad))
    return out

def singles():
    for name, _, _, _ in SCREENS:
        im = bezel(name, 8, 40, 36)
        canvas = Image.new("RGB", (im.width + 96, im.height + 96), BLACK)
        canvas.paste(im, (48, 48), im)
        canvas.save(os.path.join(OUT, name + ".png"), optimize=True)

def sheet(out, screens, sub):
    cols, scale, pad = 4, 5, 24
    tile = bezel(screens[0][0], scale, pad, 22)
    tw, th = tile.size
    margin, gx, cap_h, gy = 96, 64, 64, 40
    rows = (len(screens) + cols - 1) // cols
    head = 250
    W = 2 * margin + cols * tw + (cols - 1) * gx
    H = head + rows * (th + cap_h) + (rows - 1) * gy + 150
    im = Image.new("RGB", (W, H), BLACK)
    d = ImageDraw.Draw(im)
    d.rectangle([margin, 92, margin + 56, 98], fill=ORANGE)
    d.text((margin, 118), "SLR Remote", font=font(64, True), fill=LIGHT)
    d.text((margin, 196), sub, font=font(30), fill=GREY)
    fcap = font(30, True)
    fnum = font(30)
    for i, (name, cap, _, _) in enumerate(screens):
        r, c = divmod(i, cols)
        x = margin + c * (tw + gx)
        y = head + r * (th + cap_h + gy)
        t = bezel(name, scale, pad, 22)
        im.paste(t, (x, y), t)
        num = "%02d" % (i + 1)
        d.text((x + 4, y + th + 18), num, font=fnum, fill=ORANGE)
        d.text((x + 4 + d.textlength(num + "  ", font=fnum), y + th + 18), cap, font=fcap, fill=LIGHT)
    foot = "Drawn by the firmware's own screen code running on a PC. Example values. Not photographs."
    d.text((margin, H - 96), foot, font=font(26), fill=GREY)
    im.save(os.path.join(OUT, out), optimize=True)

if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    singles()
    # the short sheet for the README, then every screen for the screens README
    sheet("screens-sheet.png", [s for s in SCREENS if s[3]], "What you see on the handheld, firmware 0.4.0")
    sheet("screens-all.png", SCREENS, "Screens on the 128 x 64 OLED, firmware 0.4.0")
    print("wrote", len(SCREENS) + 2, "images to", OUT)
