"""Studio-style product renders of the current case set (PBR materials, image-based lighting, soft shadows).
Antennas, screen, screws and USB socket are illustrative; the case and buttons are the real print files.

Run build.py first (the renders read the exported STLs), then, from any folder:

    python case/tools/render_studio.py [hero] [handheld] [back] [top] [--out DIR]

With no view names all four are rendered. Images go to <repo>/assets/renders unless --out is given."""
import sys, os, tempfile, numpy as np, pyvista as pv
from PIL import Image, ImageDraw, ImageFont, ImageFilter
HERE = os.path.dirname(os.path.abspath(__file__))                 # <repo>/case/tools
REPO = os.path.dirname(os.path.dirname(HERE))
ARGS = sys.argv[1:]; OUT = os.path.join(REPO, 'assets', 'renders')
if '--out' in ARGS:
    i = ARGS.index('--out'); OUT = os.path.abspath(ARGS[i + 1]); del ARGS[i:i + 2]
os.makedirs(OUT, exist_ok=True)
sys.path.insert(0, os.path.join(os.path.dirname(HERE), 'final', 'miniworks_refined'))   # design.py and render_references.py
import render_references as rr
b = rr.b
pv.OFF_SCREEN = True
BODY, FRONT, ACCENT, DARK = '#0e0f11', '#868a8f', '#ff6410', '#16181b'   # black bottom (back half), grey top (front half), orange shoot button, dark reset
BG = (236, 238, 240)
MAT = {  # colour -> (metallic, roughness)
    'body': (0.0, 0.62), 'accent': (0.0, 0.5), 'rubber': (0.0, 0.85), 'steel': (1.0, 0.28), 'screw': (0.9, 0.42), 'glass': (0.0, 0.08)}
def studio_cubemap():
    """six-face environment: big soft top light, bright side strips, dark floor"""
    n = 512; faces = {}
    g = np.linspace(0, 1, n)
    wall = np.tile((0.10 + 0.55 * g[::-1] ** 1.6)[:, None], (1, n))            # brighter toward the top
    strip = wall.copy(); strip[int(n * .12):int(n * .55), int(n * .2):int(n * .8)] = 1.0   # softbox
    top = np.full((n, n), 0.95); yy, xx = np.mgrid[0:n, 0:n]; top *= np.clip(1.15 - np.hypot(xx - n / 2, yy - n / 2) / (n * .75), .35, 1)
    floor = np.full((n, n), 0.06)
    def img(a): return Image.fromarray((np.clip(a * 0.55, 0, 1) * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(10)).convert('RGB')
    order = [('posx', strip), ('negx', wall), ('posy', wall), ('negy', strip), ('posz', top), ('negz', floor)]
    d = tempfile.mkdtemp(prefix='render_studio_env_'); files = []   # scratch files, kept out of the output folder
    for name, a in order:
        p = os.path.join(d, name + '.png'); img(a).save(p); files.append(p)
    return pv.cubemap_from_filenames(files)
ENV = studio_cubemap()
def oled_texture():
    pic = Image.new('RGB', (512, 256), '#03070a'); d = ImageDraw.Draw(pic); f = lambda s: ImageFont.truetype(rr.font, s)
    d.text((22, 12), 'H-RLY', font=f(26), fill='#bfeef2'); d.text((150, 12), 'FAST', font=f(26), fill='#bfeef2'); d.rectangle((420, 14, 480, 38), outline='#bfeef2', width=3)
    d.line((20, 50, 492, 50), fill='#5a8f96', width=2)
    d.text((22, 78), 'READY', font=f(84), fill='#e6fbfd'); d.text((24, 196), 'HOLD: FOCUS', font=f(30), fill='#9fd6dc')
    return pv.numpy_to_texture(np.asarray(pic))
def unit(device, T):
    """(mesh, colour, material) for one unit, transformed by 4x4 T"""
    items = []
    A, F = rr.ACTUAL[device + '_back'], rr.ACTUAL[device + '_front']
    items += [(A, BODY, 'body'), (F, FRONT, 'body')]
    items += [(rr.ACTUAL['action_button'].translate((b.base.CX, b.base.CY, 0)), ACCENT, 'accent'),
              (rr.ACTUAL['reset_RST'].translate((b.RESET_CENTRE[0], b.RESET_CENTRE[1], b.base.REST_Z)), DARK, 'body')]
    items += [(h, '#15171a', 'screw') for h in rr.HEADS]
    for m, c in rr.antennas(device == 'camera'): items.append((m, c, 'steel' if c == '#a6a8aa' else 'rubber'))
    usb_outer = b.shell.xz_shape(b.shell.rrect(-4.4, 4.4, 15.12, 17.94, 1.0), 1.55, 2.5); usb_inner = b.shell.xz_shape(b.shell.rrect(-3.65, 3.65, 15.62, 17.44, .6), 1.5, 2.6)
    items += [(usb_outer - usb_inner, '#b9bcc0', 'steel'), (b.box(-4.3, 4.3, 2.51, 2.54, 15.2, 17.9), '#0c0f12', 'body'), (b.box(-2.8, 2.8, 1.7, 2.55, 16.35, 16.6), '#2c3035', 'body')]
    items.append((b.box(-9.2, 9.2, 16.95, 50.85, 20.0, 20.18), '#05080a', 'glass'))      # OLED glass under the window
    return [(rr._pv(m).transform(T, inplace=False), c, k) for m, c, k in items], T
def Tm(dx, dy, rz):
    c, s = np.cos(np.radians(rz)), np.sin(np.radians(rz)); return np.array([[c, -s, 0, dx], [s, c, 0, dy], [0, 0, 1, 0], [0, 0, 0, 1]], float)
def shadow_plane(meshes, centre, span=320, px=1600, shift=(5, -6)):
    """soft contact shadow baked from a top-down silhouette, on an unlit plane the same colour as the background"""
    q = pv.Plotter(off_screen=True, window_size=(px, px), lighting='none'); q.set_background('white')
    for m in meshes: q.add_mesh(m, color='black', lighting=False)
    q.enable_parallel_projection(); q.camera.position = (centre[0], centre[1], 500); q.camera.focal_point = (centre[0], centre[1], 0); q.camera.up = (0, 1, 0); q.camera.parallel_scale = span / 2
    mask = 255 - np.asarray(Image.fromarray(q.screenshot(return_img=True)[:, :, :3]).convert('L')).astype(np.float32); q.close()
    m = Image.fromarray(mask.astype(np.uint8))
    soft = np.asarray(m.filter(ImageFilter.GaussianBlur(px * 0.035)), np.float32) / 255; tight = np.asarray(m.filter(ImageFilter.GaussianBlur(px * 0.008)), np.float32) / 255
    sh = np.clip(0.38 * soft + 0.42 * tight, 0, 0.8)[:, :, None]
    rgb = (np.array(BG, np.float32)[None, None, :] * (1 - sh)).astype(np.uint8)
    plane = pv.Plane(center=(centre[0] + shift[0], centre[1] + shift[1], -0.03), direction=(0, 0, 1), i_size=span, j_size=span, i_resolution=1, j_resolution=1)
    return plane, pv.numpy_to_texture(rgb)
def render(name, units, cam, focal, up=(0, 0, 1), size=(3200, 2133), angle=24):
    p = pv.Plotter(off_screen=True, window_size=size, lighting='none'); p.set_background('#%02x%02x%02x' % BG)
    p.set_environment_texture(ENV, is_srgb=True)
    solids = []
    for dev, T in units:
        items, T = unit(dev, T)
        for mesh, col, k in items:
            solids.append(mesh)
            mesh = mesh.compute_normals(split_vertices=True, feature_angle=32, auto_orient_normals=False)
            met, rough = MAT[k]
            p.add_mesh(mesh, color=col, pbr=True, metallic=met, roughness=rough, smooth_shading=True, diffuse=1.0)
        plane = pv.Plane(center=(0, 0, 0), direction=(0, 0, 1), i_size=33.2, j_size=17.6, i_resolution=1, j_resolution=1)
        plane.rotate_z(90, inplace=True); plane.translate((0, 33.9, 20.2), inplace=True); plane.transform(T, inplace=True)
        p.add_mesh(plane, texture=oled_texture(), lighting=False)
    gp, gt = shadow_plane(solids, (focal[0], focal[1]))
    p.add_mesh(gp, texture=gt, lighting=False)
    p.add_light(pv.Light(position=(160, -190, 300), focal_point=(focal[0], focal[1], 8), color='white', intensity=2.6))
    p.add_light(pv.Light(position=(-220, 60, 140), focal_point=(focal[0], focal[1], 8), color='#dbe6ff', intensity=0.7))
    p.add_light(pv.Light(position=(40, 280, 110), focal_point=(focal[0], focal[1], 8), color='#ffeedd', intensity=0.9))
    p.enable_ssao(radius=5, bias=0.2, kernel_size=128, blur=True); p.enable_anti_aliasing('ssaa')
    p.camera.position = cam; p.camera.focal_point = focal; p.camera.up = up; p.camera.view_angle = angle
    path = os.path.join(OUT, name); p.screenshot(path); p.close(); print(path)
if __name__ == '__main__':
    pair = [('handheld', Tm(-34, 0, 12)), ('camera', Tm(38, -6, -10))]
    which = ARGS or ['hero', 'handheld', 'back', 'top']
    if 'hero' in which: render('01_hero_both.png', pair, (150, -230, 190), (4, 34, 8))
    if 'handheld' in which: render('02_handheld.png', [('handheld', Tm(0, 0, 0))], (95, -120, 95), (0, 28, 9), angle=20)
    if 'back' in which: render('03_side_two_tone.png', [('handheld', Tm(0, 0, 0))], (-150, 110, 42), (0, 30, 10), angle=18)
    if 'top' in which: render('04_top.png', pair, (4, 30, 420), (4, 34, 8), up=(0, 1, 0), angle=22)
