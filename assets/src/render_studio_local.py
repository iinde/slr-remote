"""Studio product renders for the README (PBR materials, image-based lighting, soft contact shadows).

Run from case/final/miniworks_refined so the case modules import:

    cd case/final/miniworks_refined
    python ../../../assets/src/render_studio_local.py            # all views, 3200 px wide + 1600 px copies
    python ../../../assets/src/render_studio_local.py hero side  # just some views
    PREVIEW=4 python ../../../assets/src/render_studio_local.py  # quarter-size test renders into a temp folder

The case halves and both buttons are the real print files. The antennas, screen, screws, USB socket,
board and battery are illustrative stand-ins and are not printable parts.
"""
import sys, os, tempfile, numpy as np
_ARGS = sys.argv[1:]; sys.argv = sys.argv[:1]            # render_references reads argv[1] as an output folder
import pyvista as pv, manifold3d as M
from PIL import Image, ImageDraw, ImageFont, ImageFilter
sys.path.insert(0, os.getcwd())
import render_references as rr
b = rr.b
HERE = os.path.dirname(os.path.abspath(__file__))
PREVIEW = int(os.environ.get('PREVIEW', '0'))
TMP = os.environ.get('RENDER_TMP') or tempfile.mkdtemp(prefix='slr_render_')
OUT = os.path.join(TMP, 'preview') if PREVIEW else os.path.normpath(os.path.join(HERE, '..', 'renders'))
os.makedirs(OUT, exist_ok=True)
pv.OFF_SCREEN = True
BODY, FRONT, ACCENT, DARK = '#0e0f11', '#868a8f', '#ff6410', '#16181b'   # black bottom, grey top, orange shoot button
BOARD, BATTERY = '#565b61', '#2f5d9b'
BG = (236, 238, 240)
MAT = {  # name -> (metallic, roughness)
    'body': (0.0, 0.62), 'accent': (0.0, 0.5), 'rubber': (0.0, 0.85), 'steel': (1.0, 0.28), 'screw': (0.9, 0.42), 'glass': (0.0, 0.08),
    'board': (0.0, 0.7), 'cell': (0.0, 0.45)}
DIN = '/System/Library/Fonts/Supplemental/DIN Alternate Bold.ttf'
DINC = '/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf'
# OLED window in the top is 17.2 x 32.7 mm centred on y = 33.9. The lit area of a 0.96 inch panel is smaller.
WIN_Y, LIT = 33.9, (24.0, 12.0)


def studio_cubemap():
    """six-face environment: big soft top light, bright side strips, dark floor"""
    n = 512
    g = np.linspace(0, 1, n)
    wall = np.tile((0.10 + 0.55 * g[::-1] ** 1.6)[:, None], (1, n))
    strip = wall.copy(); strip[int(n * .12):int(n * .55), int(n * .2):int(n * .8)] = 1.0
    top = np.full((n, n), 0.95); yy, xx = np.mgrid[0:n, 0:n]; top *= np.clip(1.15 - np.hypot(xx - n / 2, yy - n / 2) / (n * .75), .35, 1)
    floor = np.full((n, n), 0.06)
    def img(a): return Image.fromarray((np.clip(a * 0.55, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(10)).convert('RGB')
    d = os.path.join(TMP, '_env'); os.makedirs(d, exist_ok=True); files = []
    for name, a in [('posx', strip), ('negx', wall), ('posy', wall), ('negy', strip), ('posz', top), ('negz', floor)]:
        p = os.path.join(d, name + '.png'); img(a).save(p); files.append(p)
    return pv.cubemap_from_filenames(files)


ENV = studio_cubemap()


def oled_texture(device):
    """the real idle screens: the 128 x 64 frames drawn by the firmware's own screen code (see src/oled),
    scaled with no smoothing so the pixels stay crisp"""
    name = 'ready' if device == 'handheld' else 'camera-unit'
    lit = Image.open(os.path.join(HERE, 'oled', 'frames', name + '.pgm')).convert('L').resize((1024, 512), Image.NEAREST)
    pic = Image.new('RGB', lit.size, '#020405'); pic.paste(Image.new('RGB', lit.size, '#eaf6f8'), (0, 0), lit)
    return pv.numpy_to_texture(np.asarray(pic))


def Tm(dx, dy, rz, dz=0.0):
    c, s = np.cos(np.radians(rz)), np.sin(np.radians(rz)); return np.array([[c, -s, 0, dx], [s, c, 0, dy], [0, 0, 1, dz], [0, 0, 0, 1]], float)


def usb_socket():
    outer = b.shell.xz_shape(b.shell.rrect(-4.4, 4.4, 15.12, 17.94, 1.0), 1.55, 2.5); inner = b.shell.xz_shape(b.shell.rrect(-3.65, 3.65, 15.62, 17.44, .6), 1.5, 2.6)
    return [(outer - inner, '#b9bcc0', 'steel'), (b.box(-4.3, 4.3, 2.51, 2.54, 15.2, 17.9), '#0c0f12', 'body'), (b.box(-2.8, 2.8, 1.7, 2.55, 16.35, 16.6), '#2c3035', 'body')]


def buttons():
    return [(rr.ACTUAL['action_button'].translate((b.base.CX, b.base.CY, 0)), ACCENT, 'accent'),
            (rr.ACTUAL['reset_RST'].translate((b.RESET_CENTRE[0], b.RESET_CENTRE[1], b.base.REST_Z)), DARK, 'body')]


def screen_plane(T, z=20.2):
    plane = pv.Plane(center=(0, 0, 0), direction=(0, 0, 1), i_size=LIT[0], j_size=LIT[1], i_resolution=1, j_resolution=1)
    plane.rotate_z(90, inplace=True); plane.translate((0, WIN_Y, z), inplace=True); plane.transform(T, inplace=True)
    return plane


def unit(device, T, upright=None):
    """one assembled unit: list of (mesh, colour, material) and its screen plane.
    upright: antenna folded up at its hinge (the camera unit's default) or lying straight out"""
    if upright is None: upright = device == 'camera'
    items = [(rr.ACTUAL[device + '_back'], BODY, 'body'), (rr.ACTUAL[device + '_front'], FRONT, 'body')] + buttons()
    items += [(h, '#15171a', 'screw') for h in rr.HEADS]
    for m, c in rr.antennas(upright): items.append((m, c, 'steel' if c == '#a6a8aa' else 'rubber'))
    items += usb_socket()
    items.append((b.box(-9.2, 9.2, 16.95, 50.85, 20.0, 20.18), '#05080a', 'glass'))      # OLED glass under the window
    return [(rr._pv(m).transform(T, inplace=False), c, k) for m, c, k in items], [(screen_plane(T), device)]


def exploded(T, gap=13.0):
    """the handheld pulled apart along z with even gaps: bottom, battery, board, buttons, top, screws.
    The buttons have a flange and go in from inside the top, so they sit under it here."""
    up = lambda m, dz: m.translate((0, 0, dz))
    items = []; z = 0.0
    back = rr.ACTUAL['handheld_back']; items.append((back, BODY, 'body')); z = 15.03 + gap
    cell = b.box(-17.0, 17.0, 5.5, 54.5, 0, 6.0); items.append((up(cell, z), BATTERY, 'cell'))          # LP603449: 6 x 34 x 49 mm
    items.append((up(b.box(-17.02, 17.02, 5.48, 9.5, 0.6, 5.4), z), '#c9a23a', 'cell'))                 # the cell's tape end
    z += 6.0 + gap
    pcb = b.box(-12.7, 12.7, 2.6, 54.4, 0, 1.6); items.append((up(pcb, z), BOARD, 'board'))             # board outline, approximate
    items.append((up(b.box(-9.2, 9.2, 16.95, 50.85, 1.6, 3.2), z), '#05080a', 'glass'))                 # OLED
    items.append((up(b.box(-4.4, 4.4, 2.0, 9.3, 1.6, 4.4), z), '#a9adb2', 'board'))                     # USB-C socket
    screen_z = z + 3.22; z += 4.4 + gap
    for m, c, k in buttons(): items.append((up(m, z - m.bounding_box()[2]), c, k))
    z += 5.0 + gap
    front = rr.ACTUAL['handheld_front']; items.append((up(front, z - b.S), FRONT, 'body')); z += (b.H - b.S) + gap
    for sx in (-1, 1):
        for y in b.shell.SCREW_Y:
            x = sx * b.shell.SCREW_X
            head = b.cyl(x, y, 16.0, 18.0, 1.85) - M.CrossSection.circle(.78, 6).extrude(.8).translate((x, y, 17.25))
            items.append((up(head + b.cyl(x, y, 0, 16.0, 0.95), z), '#15171a', 'screw'))                # M2 x 16
    return [(rr._pv(m).transform(T, inplace=False), c, k) for m, c, k in items], [(screen_plane(T, screen_z), 'handheld')], z + 18.0


def shadow_plane(meshes, centre, span=320, px=1600, shift=(5, -6), strength=1.0):
    """soft contact shadow baked from a top-down silhouette, on an unlit plane the same colour as the background"""
    q = pv.Plotter(off_screen=True, window_size=(px, px), lighting='none'); q.set_background('white')
    for m in meshes: q.add_mesh(m, color='black', lighting=False)
    q.enable_parallel_projection(); q.camera.position = (centre[0], centre[1], 900); q.camera.focal_point = (centre[0], centre[1], 0); q.camera.up = (0, 1, 0); q.camera.parallel_scale = span / 2
    mask = 255 - np.asarray(Image.fromarray(q.screenshot(return_img=True)[:, :, :3]).convert('L')).astype(np.float32); q.close()
    m = Image.fromarray(mask.astype(np.uint8))
    soft = np.asarray(m.filter(ImageFilter.GaussianBlur(px * 0.035)), np.float32) / 255; tight = np.asarray(m.filter(ImageFilter.GaussianBlur(px * 0.008)), np.float32) / 255
    sh = (np.clip(0.38 * soft + 0.42 * tight, 0, 0.8) * strength)[:, :, None]
    rgb = (np.array(BG, np.float32)[None, None, :] * (1 - sh)).astype(np.uint8)
    plane = pv.Plane(center=(centre[0] + shift[0], centre[1] + shift[1], -0.03), direction=(0, 0, 1), i_size=span, j_size=span, i_resolution=1, j_resolution=1)
    return plane, pv.numpy_to_texture(rgb)


def render(name, groups, cam, focal, up=(0, 0, 1), size=(3200, 2133), angle=24, shadow=1.0, shift=(5, -6), bias=0.2):
    """groups: list of (items, screens) as returned by unit() / exploded()"""
    if PREVIEW: size = (size[0] // PREVIEW, size[1] // PREVIEW)
    p = pv.Plotter(off_screen=True, window_size=size, lighting='none'); p.set_background('#%02x%02x%02x' % BG)
    p.set_environment_texture(ENV, is_srgb=True)
    solids = []
    for items, screens in groups:
        for mesh, col, k in items:
            solids.append(mesh)
            mesh = mesh.compute_normals(split_vertices=True, feature_angle=32, auto_orient_normals=False)
            met, rough = MAT[k]
            p.add_mesh(mesh, color=col, pbr=True, metallic=met, roughness=rough, smooth_shading=True, diffuse=1.0)
        for plane, device in screens: p.add_mesh(plane, texture=oled_texture(device), lighting=False)
    gp, gt = shadow_plane(solids, (focal[0], focal[1]), strength=shadow, shift=shift)
    p.add_mesh(gp, texture=gt, lighting=False)
    p.add_light(pv.Light(position=(160, -190, 300), focal_point=(focal[0], focal[1], 8), color='white', intensity=2.6))
    p.add_light(pv.Light(position=(-220, 60, 140), focal_point=(focal[0], focal[1], 8), color='#dbe6ff', intensity=0.7))
    p.add_light(pv.Light(position=(40, 280, 110), focal_point=(focal[0], focal[1], 8), color='#ffeedd', intensity=0.9))
    p.enable_ssao(radius=5, bias=float(os.environ.get('BIAS', bias)), kernel_size=128, blur=True); p.enable_anti_aliasing('ssaa')
    p.camera.position = cam; p.camera.focal_point = focal; p.camera.up = up; p.camera.view_angle = angle
    path = os.path.join(OUT, name + '.png'); p.screenshot(path); p.close()
    im = Image.open(path).convert('RGB'); im.save(path, optimize=True)
    if not PREVIEW:
        w = 1600; im.resize((w, round(im.height * w / im.width)), Image.LANCZOS).save(os.path.join(OUT, name + '-1600.png'), optimize=True)
    print(path, im.size, flush=True)


def orbit(focal, dist, az, el):
    """camera position at a distance from the focal point; az in degrees from +x toward +y, el above the horizon"""
    a, e = np.radians(az), np.radians(el)
    return (focal[0] + dist * np.cos(e) * np.cos(a), focal[1] + dist * np.cos(e) * np.sin(a), focal[2] + dist * np.sin(e))


VIEWS = ['hero', 'handheld', 'side', 'pair-top', 'exploded', 'buttons']
if __name__ == '__main__':
    which = _ARGS or VIEWS
    I = Tm(0, 0, 0)
    # The screen text runs along the long side with its top toward -x, so every camera sits on the +x side.
    if 'hero' in which:
        f = (1, -6, 14); az = -38
        r = np.array([-np.sin(np.radians(az)), np.cos(np.radians(az))]); back = np.array([-r[1], r[0]])   # screen right, and away from the camera
        def at(device, centre, rz):   # put the middle of a unit's body at a point on the floor
            half = (81.28 if device == 'camera' else 60.96) / 2; c, s_ = np.cos(np.radians(rz)), np.sin(np.radians(rz))
            return unit(device, Tm(centre[0] + s_ * half, centre[1] - c * half, rz))
        render('hero', [at('camera', -53 * r + 20 * back, 7), at('handheld', 44 * r - 22 * back, -7)], orbit(f, 365, az, 34), f)
    if 'handheld' in which:
        f = (-7, 35, 7)
        render('handheld', [unit('handheld', I)], orbit(f, 262, -40, 32), f, angle=20)
    if 'side' in which:
        f = (0, 47, 11)   # three-quarter view from the antenna end: grip band, rolled back edge, lit screen
        render('side', [unit('handheld', I)], orbit(f, 285, 32, 13), f, angle=18)
    if 'pair-top' in which:
        f = (0, 44, 8)
        render('pair-top', [unit('handheld', Tm(-31, 0, 0)), unit('camera', Tm(31, 0, 0), upright=False)], (f[0], f[1], 420), f, up=(-1, 0, 0), angle=22, shift=(4, -4))
    if 'exploded' in which:
        items, screens, top = exploded(I)
        f = (0, 30.5, top / 2 - 2)
        render('exploded', [(items, screens)], orbit(f, 560, -31, 16), f, size=(3200, 4000), angle=17, shadow=0.75, bias=1.2)   # long shot: a larger bias stops faint banding in the ambient shading
    if 'buttons' in which:
        f = (-1, 17, 19)
        render('buttons', [unit('handheld', I)], orbit(f, 118, -52, 46), f, angle=22)
