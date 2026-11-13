"""Renders the six printable case parts and lays them out as a labelled sheet.

Run:  python make_case_parts.py
Sizes under each part are read from the STL bounds, in the orientation the part prints in.
"""
import os
import numpy as np
import pyvista as pv
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
STL = os.path.join(REPO, "case", "final", "miniworks_refined", "print")
OUT = os.path.join(REPO, "assets", "diagrams", "case-parts.png")

BG, BLACK, GRAPHITE, GREY, ORANGE = "#f6f7f8", "#0e0f11", "#23272b", "#868a8f", "#ff6410"
LINE = "#d9dcdf"
HN = "/System/Library/Fonts/HelveticaNeue.ttc"
BUTTON_ZOOM = 5

def din(size):
    return ImageFont.truetype("/System/Library/Fonts/Supplemental/DIN Alternate Bold.ttf", size)

def avenir(size, style="Demi Bold"):
    for i in range(12):
        f = ImageFont.truetype("/System/Library/Fonts/Avenir Next.ttc", size, index=i)
        if f.getname()[1] == style:
            return f
    raise SystemExit("no Avenir Next style " + style)

def font(size, bold=False):
    return ImageFont.truetype(HN, size, index=1 if bold else 0)

# stl, name, print plate, note, is a button
PARTS = [
    ("miniworks_refined_handheld_back.stl", "Handheld bottom", "Plate 1", "Holds the board and battery", False),
    ("miniworks_refined_handheld_front.stl", "Handheld top", "Plate 1", "Screen window and button holes", False),
    ("miniworks_refined_action_button.stl", "Shoot button", "Plate 3", "Print two, one per unit", True),
    ("miniworks_refined_camera_back.stl", "Camera unit bottom", "Plate 2", "Flat back with the GPS bay", False),
    ("miniworks_refined_camera_front.stl", "Camera unit top", "Plate 2", "Screen window and button holes", False),
    ("miniworks_refined_reset_RST.stl", "Reset button", "Plate 3", "Print two, one per unit", True),
]

TW, TH, SS = 690, 600, 2   # tile size in px, supersample

def render(path, button):
    mesh = pv.read(path)
    ext = np.array(mesh.bounds).reshape(3, 2)
    size = ext[:, 1] - ext[:, 0]
    mesh = mesh.translate(-ext.mean(axis=1), inplace=False)
    pl = pv.Plotter(off_screen=True, window_size=(TW * SS, TH * SS), lighting="none")
    pl.set_background(BG)
    pl.add_mesh(mesh, color="#9a9ea3", smooth_shading=True, split_sharp_edges=True, feature_angle=35,
                ambient=0.32, diffuse=0.75, specular=0.08, specular_power=12)
    pl.add_light(pv.Light(position=(-60, -120, 160), focal_point=(0, 0, 0), intensity=0.95, light_type="scene light"))
    pl.add_light(pv.Light(position=(140, 40, 90), focal_point=(0, 0, 0), intensity=0.35, light_type="scene light"))
    pl.enable_parallel_projection()
    d = np.array([0.62, -1.0, 0.95])
    pl.camera.position = tuple(d / np.linalg.norm(d) * 400)
    pl.camera.focal_point = (0, 0, 0)
    pl.camera.up = (0, 0, 1)
    pl.camera.parallel_scale = 41.0 / (BUTTON_ZOOM if button else 1)   # half the view height, mm
    pl.enable_anti_aliasing("ssaa")
    img = pl.screenshot(return_img=True)
    pl.close()
    return Image.fromarray(img).resize((TW, TH), Image.LANCZOS), size

def main():
    W, margin, cols = 2400, 100, 3
    gap = (W - 2 * margin - cols * TW) // (cols - 1)
    head, cap_h, row_gap = 340, 200, 50
    H = head + 2 * (TH + cap_h) + row_gap + 210
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    d.rectangle([margin, 92, margin + 56, 100], fill=ORANGE)
    d.text((margin + 76, 85), "SLR REMOTE", font=din(27), fill=GRAPHITE)
    d.text((margin + 76 + d.textlength("SLR REMOTE", font=din(27)) + 22, 85), "SUPER LONG RANGE REMOTE", font=din(27), fill=GREY)
    d.text((margin - 3, 205), "Eight printed parts", font=avenir(78), fill=BLACK, anchor="ls")
    d.text((margin, 268), "Six shapes on three plates, the two buttons printed twice. Every part prints with no supports.",
           font=avenir(33, "Regular"), fill="#5b6066", anchor="ls")
    for i, (stl, name, qty, note, button) in enumerate(PARTS):
        tile, size = render(os.path.join(STL, stl), button)
        r, c = divmod(i, cols)
        x = margin + c * (TW + gap)
        y = head + r * (TH + cap_h + row_gap)
        im.paste(tile, (x, y))
        d.rounded_rectangle([x, y, x + TW - 1, y + TH - 1], 18, outline=LINE, width=2)
        if button:
            d.text((x + 24, y + 20), "Shown %d times larger" % BUTTON_ZOOM, font=font(26), fill=GREY)
        ty = y + TH + 26
        d.text((x + 4, ty), name, font=font(40, True), fill=BLACK)
        qw = d.textlength(qty, font=font(30, True))
        d.rounded_rectangle([x + TW - qw - 40, ty + 2, x + TW - 1, ty + 48], 23, fill=ORANGE)
        d.text((x + TW - qw - 20, ty + 7), qty, font=font(30, True), fill="#ffffff")
        d.text((x + 4, ty + 62), "%.1f x %.1f x %.1f mm" % tuple(size), font=font(32), fill=GRAPHITE)
        d.text((x + 4, ty + 108), note, font=font(28), fill=GREY)
    foot = ("Sizes are width x length x height as the part sits on the print bed. "
            "Each unit also needs four M2 x 16 mm screws.")
    d.text((margin, H - 170), foot, font=font(28), fill=GREY)
    d.line([(margin, H - 106), (W - margin, H - 106)], fill=LINE, width=2)
    d.text((margin, H - 62), "SLR Remote", font=avenir(25), fill=GRAPHITE, anchor="ls")
    d.text((margin + d.textlength("SLR Remote", font=avenir(25)) + 18, H - 62), "by Aaron Phull",
           font=avenir(25, "Regular"), fill="#5b6066", anchor="ls")
    d.text((W - margin, H - 62), "Case adapted from the Miniworks case by Chem_Kid.", font=avenir(25, "Regular"),
           fill="#5b6066", anchor="rs")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    im.save(OUT, optimize=True)
    print("wrote", OUT, im.size)

if __name__ == "__main__":
    main()
