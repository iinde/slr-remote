"""Shared renderer so every concept is lit and framed the same way.

    from render_kit import render_views, contact_sheet
    render_views([(mesh, '#e9e6df'), (mesh2, '#2b2f36')], 'out/hero', title='Pebble')

mesh = trimesh.Trimesh (or manifold3d Manifold, converted). Units mm. Writes <prefix>_<view>.png.
"""
from __future__ import annotations
import numpy as np
import trimesh
import pyvista as pv
from PIL import Image, ImageDraw, ImageFont

pv.OFF_SCREEN = True

# warm stone, graphite, signal accent, translucent screen
COL = dict(stone="#e7e3da", graphite="#33373e", sand="#cbbfa8", accent="#e0662a",
           screen="#0d1117", camera="#202226", pcb="#2f6b3a", battery="#9aa3ad")

VIEWS = {
    "hero":  dict(pos=(1.0, -1.25, 0.85), up=(0, 0, 1)),
    "back":  dict(pos=(-1.1, 1.0, 0.7), up=(0, 0, 1)),
    "top":   dict(pos=(0, 0, 1), up=(0, 1, 0)),
    "side":  dict(pos=(0, -1, 0.0001), up=(0, 0, 1)),
    "end":   dict(pos=(1, 0, 0.0001), up=(0, 0, 1)),
    "under": dict(pos=(0.9, -1.0, -0.9), up=(0, 0, 1)),
}


def _pv(m):
    if not isinstance(m, trimesh.Trimesh):
        mm = m.to_mesh()
        m = trimesh.Trimesh(np.asarray(mm.vert_properties)[:, :3], np.asarray(mm.tri_verts), process=True)
    faces = np.hstack([np.full((len(m.faces), 1), 3), m.faces]).astype(np.int64)
    return pv.PolyData(np.asarray(m.vertices, float), faces)


def render_views(parts, prefix, views=("hero", "back", "top", "side", "under"), size=(1600, 1200),
                 title=None, opacity=None, ortho_views=("top", "side", "end")):
    """parts: list of (mesh, colour) or (mesh, colour, opacity). Returns list of written paths."""
    paths = []
    for v in views:
        p = pv.Plotter(off_screen=True, window_size=size, lighting="three lights")
        p.set_background("white")
        for item in parts:
            m, c = item[0], item[1]
            op = item[2] if len(item) > 2 else 1.0
            p.add_mesh(_pv(m), color=c, smooth_shading=True, split_sharp_edges=True, feature_angle=35,
                       specular=0.25, specular_power=18, ambient=0.18, diffuse=0.85, opacity=op)
        p.enable_anti_aliasing("ssaa")
        cfg = VIEWS[v]
        p.camera_position = "xy"
        b = np.array(p.bounds).reshape(3, 2)
        ctr = b.mean(axis=1)
        d = np.linalg.norm(b[:, 1] - b[:, 0])
        p.camera.focal_point = ctr
        p.camera.position = ctr + np.array(cfg["pos"]) / np.linalg.norm(cfg["pos"]) * d * 2.2
        p.camera.up = cfg["up"]
        if v in ortho_views:
            p.enable_parallel_projection()
        p.reset_camera(); p.camera.zoom(1.25)
        path = f"{prefix}_{v}.png"
        p.screenshot(path)
        p.close()
        if title:
            im = Image.open(path).convert("RGB"); dr = ImageDraw.Draw(im)
            try:
                f = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 34)
            except Exception:
                f = ImageFont.load_default()
            dr.text((30, 24), f"{title}  ({v})", fill=(40, 40, 40), font=f)
            im.save(path)
        paths.append(path)
    return paths


def contact_sheet(paths, out, cols=3, cell=(800, 600), label=None):
    rows = (len(paths) + cols - 1) // cols
    top = 70 if label else 0
    sheet = Image.new("RGB", (cols * cell[0], rows * cell[1] + top), "white")
    for i, pth in enumerate(paths):
        im = Image.open(pth).convert("RGB"); im.thumbnail(cell)
        sheet.paste(im, ((i % cols) * cell[0], (i // cols) * cell[1] + top))
    if label:
        dr = ImageDraw.Draw(sheet)
        try:
            f = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 44)
        except Exception:
            f = ImageFont.load_default()
        dr.text((30, 14), label, fill=(20, 20, 20), font=f)
    sheet.save(out)
    return out
