#!/usr/bin/env python3
"""camera_block: CAMERA VERSION A "Rangefinder block", print-ready.

    python final/camera_block/build.py              # geometry, checks, STLs, 3MF, renders
    python final/camera_block/build.py --no-render  # skip the PNGs
    (--no-sweep skips the 14-rebuild knob sweep; --probe is the sweep's worker mode)

Derived from an earlier flat camera-block concept. Silver cap over a black body, long axis on
the lens axis, a separate ISO 518 shoe foot printed standing, keyed and held by 2 x M2 self-tappers, leaf spring
click into the shoe lock-pin hole, black fairing hiding the shoe, felt pads.

Changes for print-ready final:
  * USB-C: open U-notch in the cover so a 12 x 6.5 r1 cable overmold seats fully against the receptacle.
  * RST is a second button: a recessed (0.8) square flexure tile, hinged on its outboard (-x) edge, with a nub on the switch.
    Its slit is printed closed with a one-layer (0.2) sacrificial skin under the recess floor, cut once with a knife.
  * PRG: flush round disc with a C-slit and an orange dimple (the main thumb button), 0.8 flexure skin.
  * Antenna: routed groove in the cover, 45 deg side-entry snap mouth (1.3 for a 1.6 sleeve), >= 1 mm plastic
    under the cable on the lanes over the cell, bends filleted r5, never near the seam.
  * Board located sideways by fences on the base ledges. Lead bay 5.3 behind the cell (holds the mated SH1.25 plug,
    its wire bend and the splice).
  * Antenna and cover: antenna service loop in a longer nose bay (cover opens on its front edge, pry notches at
    the rear), PRG chamfer rings kept off the hinge, leaf spring along y inside the shoe-foot neck, window = OLED bracket
    + 0.3, clear filament light pipe for the charge LED.
  * Plug clearance: board, cell and everything on them moved 2.3 forward so the mated SH1.25 plug (3.0
    [EST]) and its wire bend (1.5 [EST]) fit behind the socket mouths (body 66.9 -> 69.2), checked as keep-outs; print-only
    mouse ears on the standing foot (150 mm2 of bed, brim stored OFF per object); one 3MF plate per colour.
  * Opening the case: rear V pry notches replaced by the coupon coin slot (12.0 x 2.2, 1.35 deep, base only,
    cover whole) in each side wall at y 20..32, with the coupon coin test.
  * Shoe foot: keys + screws follow the shoe (REAR_OVERHANG sweep self-test, dropped shards fail),
    RST hinge moved to the outboard edge clear of the light pipe, light pipe held by a 1.70 press-fit neck, foot leaf
    lengthened (strain 0.58%, derated limit 0.7% as it bends across the layers) with 45 deg root fillets.
  * Every part is checked: keep-outs, wall thickness, per-layer overhang/bridge/ledge/island, snaps, antenna,
    USB, buttons, watertight single body, layer grid.

Device frame (mm): x across the camera (+ = right seen from behind), y along the lens axis (0 = rear face, + =
toward the subject), z up (0 = body underside, the foot hangs below). Board (u, v, w): x = v - 12.5, y = u + U0,
z = w + PCB_Z1.
"""
from __future__ import annotations
import math, os, sys, zipfile
import numpy as np
import manifold3d as mf
from manifold3d import Manifold as M, CrossSection as CS, JoinType
import trimesh
from matplotlib.path import Path as MPath

HERE = os.path.dirname(os.path.abspath(__file__))
CAD = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(CAD, "concepts"))
from render_kit import render_views, contact_sheet, COL, VIEWS  # noqa: E402

mf.set_circular_segments(64)
E = 0.01
ID = "camera_block"
LAYER = 0.2


# ===================================================================== ESTIMATED NUMBERS (MEASURE these)
class EST:
    """Every number here is an estimate. Values carried over from the earlier slim design keep their tag.
    MEASURE = check on the real part and edit here; everything else follows."""
    # ---- Heltec WiFi LoRa 32 V4 (board frame u along the board from the USB end, v across, w up from PCB top)
    pcb_l = 52.0             # [EST] MEASURE board length
    pcb_w = 25.0             # [EST] MEASURE board width
    pcb_t = 1.6              # [EST] MEASURE (datasheet 1.2-1.6)
    usb_chamfer = 1.5        # [EST] USB-end corner chamfers
    nose_u = 48.0            # [EST] full width up to here, then chamfers to the tip
    tip_v = (6.3, 18.7)      # [EST] flat nose tip at u = 52
    bracket_u = (14.9, 47.8) # [EST] MEASURE OLED bracket extent
    bracket_v = (2.5, 22.5)  # [EST]
    bracket_top = 5.8        # [EST] OLED bracket top above the PCB top (brief)
    bracket_r = 1.5          # [EST]
    usb_w = 9.0              # [EST] receptacle shell width
    usb_z = (0.4, 3.6)       # [EST] MEASURE receptacle bottom/top above the PCB top
    usb_proud = 0.7          # [EST] MEASURE receptacle face sticks out past the board edge (u = -0.7)
    usb_len = 7.3            # [EST]
    btn_u = 2.8              # [EST] MEASURE PRG and RST switch centre u (brief)
    prg_v = 20.5             # [EST] MEASURE (brief)
    rst_v = 4.5              # [EST] MEASURE (brief)
    btn_top = 2.3            # [EST] MEASURE actuator top above the PCB top
    btn_body = (3.5, 4.0)    # [EST] switch body u x v
    btn_travel = 0.25        # [EST] MEASURE tact switch travel to click
    led_u = (7.0, 9.0)       # [EST] charge LEDs (brief)
    led_v = (3.0, 5.0)       # [EST]
    led_h = 0.6              # [EST]
    top_parts_h = 1.5        # [EST] MEASURE tallest other top-side part between USB and bracket / at the nose
    glass_u = (18.75, 43.95) # [EST] MEASURE OLED glass extent along the board (the window now shows the whole bracket)
    glass_v = (5.9, 19.1)    # [EST] MEASURE OLED glass extent across the board; also where the active area sits on it
    ipex_uv = (49.6, 12.5)   # [EST] LoRa IPEX socket (brief)
    ipex_h = 2.0             # [EST] mated plug height
    ipex_d = 2.0             # [EST]
    ipex_relief = 0.6        # [DES] keep-clear round the plug
    cable_z = 1.4            # [EST] pigtail centre height where it leaves the plug
    sock_u = (0.0, 5.5)      # [EST] SH1.25 Bat + Sol sockets, underside, mouths face -u
    sock_v = (2.0, 23.0)     # [EST]
    sock_drop = 3.5          # [EST] below the PCB bottom (brief)
    plug_proud = 3.0         # [EST] MEASURE mated SH1.25 plug housing: how far it stands proud of the socket mouth (-u)
    plug_bend = 1.5          # [EST] MEASURE room behind the plug for the wires to leave along -u and turn 90 deg down
    gnss_u = (41.0, 48.0)    # [EST] unused underside socket
    gnss_v = (3.0, 22.0)     # [EST]
    under_h = 1.2            # [EST] all other underside parts
    nose_free_u = 50.9       # [EST] MEASURE nose-tip underside strip u 50.9..52 free of parts (board rests here)
    pad_strip = 2.2          # [EST] header pad strips along both long edges (flat, the board is clamped here)
    # ---- cell: EEMB LP603449
    batt = (51.0, 34.5, 6.3)  # [DS] MAX L x W x H
    batt_tol = 0.5            # [EST] brief: allow +0.5 on L and W
    batt_swell = 0.3          # [EST] brief: allow 0.3 swell
    lead_bay = 3.0            # [EST] MEASURE splice (JST lead to SH1.25 pigtail) needs this much length behind the cell
    # ---- antenna
    ant_len = 100.0           # [EST] MEASURE floppy LoRa antenna, IPEX plug to tip
    ant_d = 1.6               # [EST] MEASURE sleeve diameter (brief)
    ant_bend_r = 4.0          # [EST] minimum bend radius (brief)
    # ---- USB-C cable
    usb_env = (12.0, 6.5, 15.0, 1.0)  # [EST] MEASURE overmold w x h x length, corner r (brief)
    usb_cable_d = 4.0         # [EST] cable behind the overmold (for the on-camera reach check)
    # ---- charge LED light pipe (clear 1.75 filament offcut, push fit)
    pipe_d = 1.75             # [EST] MEASURE the clear filament you use (1.75 nominal)
    # ---- X-E5 (no published drawing; every camera number is an estimate)
    shoe_floor = 1.0          # [EST] MEASURE shoe floor above the top-plate paint
    rail_h = 2.4              # [EST] MEASURE shoe rail top above the shoe floor
    lockpin_back = 2.5        # [EST] MEASURE lock-pin hole centre, behind the shoe's front edge (and is there one?)
    shoe_to_rear = 15.5       # [EST] MEASURE shoe centre to the camera rear face
    ss_dial_edge = 21.0       # [EST] MEASURE shoe centre line to the shutter speed dial knurl (to the right)
    fs_dial_edge = 27.5       # [EST] MEASURE shoe centre line to the film simulation dial (to the left)
    ss_dial_h = 8.0           # [EST] MEASURE dial top above the paint
    fs_dial_h = 6.5           # [EST]
    heel_flat = (2.0, 5.6, 24.0)  # [EST] MEASURE flat paint behind the shoe: y from camera rear, to y, width


# ===================================================================== design parameters
class D:
    FOOT_X = 3.0           # [DES] PARAM body sits this far LEFT of the shoe centre line (set from the dial MEASURE)
    REAR_OVERHANG = 0.0    # [DES] PARAM body rear face behind the camera rear face (+ = further back toward the eye)
    DIAL_MIN_GAP = 2.0     # [DES] minimum side gap from the body to either dial
    WALL = 2.4             # [DES] side/front wall: the snap slot keeps a 1.05 outer skin
    REAR_WALL = 1.6        # [DES]
    PROUD = 0.3            # [DES] silver cap overhangs the black body
    FLOOR = 2.2            # [DES]
    H = 21.2               # [DES] body height (106 layers)
    R_PLAN, R_TOP, R_BOT = 8.0, 3.0, 3.0


# the self-test sweep re-runs this file with the two MEASURE knobs overridden (see --probe / selftest_sweep())
for _k in ("FOOT_X", "REAR_OVERHANG"):
    if os.environ.get("CB_" + _k):
        setattr(D, _k, float(os.environ["CB_" + _k]))


P_ISO = dict(plate=(18.0, 18.0, 1.8), neck=12.0)   # [DS] ISO 518 foot plate, neck width

# ------------------------------------------------------------------ derived layout
CELL_L = EST.batt[0] + EST.batt_tol             # 51.5 keep-out
CELL_W = EST.batt[1] + EST.batt_tol             # 35.0
CELL_H = EST.batt[2] + EST.batt_swell           # 6.6
POCKET_W = 35.3                                 # 0.15 per side round the 35.0 keep-out
CAV_X = POCKET_W / 2
WALL, REAR_WALL, PROUD = D.WALL, D.REAR_WALL, D.PROUD
W2 = CAV_X + WALL                               # 20.05 -> cover 40.1 wide
Y_IN0 = REAR_WALL
# board USB edge (= the SH1.25 socket mouths, which face -u) sits this far in from the pocket rear face: the mated plug
# (3.0 [EST]) + the wire exit and 90 deg bend (1.5 [EST]) + 0.2 clear. Was 2.4, which the mated plug alone overran.
# The sockets span x -10.5..10.5, inside the flat of the rear face (the R5 corners start at |x| 12.65).
USB_WALL_U = -(EST.plug_proud + EST.plug_bend + 0.2)    # -4.7
U0 = Y_IN0 - USB_WALL_U                         # 6.3
POCKET_R_REAR, POCKET_R_FRONT = 5.0, 3.5          # rear R5 keeps the R8 plan corner wall >= 1.0
CELL_Y0 = max(Y_IN0 + EST.lead_bay, U0 + 0.6)   # 6.9 cell rear: 0.6 in front of the board's USB edge as before; the bay
#                                                 behind it (5.3) now holds the plug, the wire bend and the splice
CELL_Y1 = CELL_Y0 + CELL_L                      # 56.1
# front pocket corners (R3.5) must clear the cell's r1 corners: needs 1.65 nose gap
_nose_gap = POCKET_R_FRONT - 1.0 - math.sqrt((POCKET_R_FRONT - 1.0) ** 2 - (CELL_W / 2 - 1.0 - (CAV_X - POCKET_R_FRONT)) ** 2)
Y_CELL_FRONT = math.ceil((CELL_Y1 + _nose_gap) * 10 - 1e-6) / 10   # 57.8: where the pocket would end for the cell alone
# antenna service loop: a loose U (r LOOP_R) lying in a nose bay in front of the OLED bracket, so the cover can stand
# on its front edge with the IPEX plugged in. Leg A runs at the plug, leg B LOOP_R*2 in front of it.
LOOP_R = 4.2                                    # [DES] U-turn radius (>= 4.0 bend)
LOOP_X = 8.0                                    # [DES] U-turn axis x (sets the free length)
LOOP_YA = EST.ipex_uv[0] + U0                   # 53.6 (plug)
LOOP_YB = LOOP_YA + 2 * LOOP_R                  # 62.0
BAY_GAP = 1.6                                   # [DES] free loop to the cover's front face (>= 1.5, it closes downward)
Y_IN1 = max(Y_CELL_FRONT, math.ceil((LOOP_YB + EST.ant_d / 2 + BAY_GAP + 0.1) * 10 - 1e-6) / 10)   # 64.5
L = Y_IN1 + WALL                                # 69.2
STOP_Y0 = CELL_Y1 + 0.15                        # cell stop wall in front of the cell (the pocket is longer than the cell now)
STOP_T = 1.2
BAY_X = 14.0                                    # the cover's inner lip is removed over |x| <= BAY_X at the nose
FLOOR = D.FLOOR
BATT_Z0, BATT_Z1 = FLOOR, FLOOR + CELL_H        # 2.2 .. 8.8
AIR_GAP = 3.8                                   # [DES] cell top to PCB bottom (3.5 socket drop + 0.3)
PCB_Z0 = BATT_Z1 + AIR_GAP                      # 12.6
PCB_Z1 = PCB_Z0 + EST.pcb_t                     # 14.2
BRK_TOP = PCB_Z1 + EST.bracket_top              # 20.0
BRK_CLR = 0.2                                   # skin over the OLED bracket = 1.0
BRK_LAT = 0.25                                  # side clearance round the OLED bracket
H = D.H
TOP_IN = H - 1.4                                # 19.8 top skin 1.4
SPLIT = PCB_Z0                                  # 12.6 seam (63 layers)
R_PLAN, R_TOP, R_BOT = D.R_PLAN, D.R_TOP, D.R_BOT
TONGUE_T, TONGUE_H, GROOVE_CLR = 0.9, 1.6, 0.1

# snaps
SNAP_YS = (15.0, 45.0)
ARM_W, ARM_T = 6.0, 0.8
ROOT_F = 0.5
BUMP = 0.4                                      # interference = BUMP - GROOVE_CLR = 0.30
BUMP_Z = (SPLIT + 5.6, SPLIT + 6.0, SPLIT + 6.4, SPLIT + 6.8)   # retention 45, flat, lead-in 45 (all on the 0.2 grid)
ARM_TOP = SPLIT + 6.8
ARM_RELIEF = 0.55

# shoe / foot
PLATE_W, PLATE_L, PLATE_T = P_ISO["plate"]
NECK_W = P_ISO["neck"]
WEB_T = 1.0                                     # web under the body (>= 1.0 wall)
RAIL_H = EST.rail_h
RAIL_GAP = 0.4
Z_WEB0 = -WEB_T
Z_SHOE = Z_WEB0 - RAIL_GAP - RAIL_H             # -3.6
Z_PL1 = Z_SHOE + PLATE_T
Z_PAINT = Z_SHOE - EST.shoe_floor               # -4.6
FELT_T = 0.6
CAM_REAR_Y = EST.shoe_to_rear
SHOE_Y = CAM_REAR_Y + D.REAR_OVERHANG           # shoe centre in device y
FOOT_X = D.FOOT_X
PL_Y0, PL_Y1 = SHOE_Y - PLATE_L / 2, SHOE_Y + PLATE_L / 2
FL_Y0, FL_Y1 = PL_Y1 - 22.6, PL_Y1               # foot prints 22.6 tall (113 layers)
RAIL_END_Y = SHOE_Y - 9.0                       # [EST] shoe rails end 9 behind the shoe centre
FAIR_IN, FAIR_T = 11.0, 1.2
FAIR_Z = Z_PAINT + 1.5
HEEL = dict(y0=FL_Y0, z=Z_PAINT + 1.0 - 0.2)    # 1.0 felt crushed 0.2
HEEL["y1"] = RAIL_END_Y - 0.3 - (Z_SHOE + EST.rail_h - HEEL["z"])   # front of the pad; front face leans FORWARD 45 going up
# every foot-to-base interface (screws, key) is tied to the shoe, so REAR_OVERHANG moves them with the foot
SCREWS = [(FOOT_X - 4.2, SHOE_Y - 6.0), (FOOT_X + 4.2, SHOE_Y - 5.0)]   # second screw clears the longer leaf cavity by >= 1.0
KEY_CROSS_Y = FL_Y0 + 1.7                       # cross key centre (was a fixed 3.6 with the foot at FL_Y0 1.9)
KEY_LONG_Y0 = FL_Y0 + 3.7                       # long key start (was a fixed 5.6)
PILOT_D = 3.0
# leaf spring runs ALONG y inside the neck: root 10 behind the plate front, free tip at the front edge. Every gap round
# it is a vertical slot in print pose (foot stands on its front end), so nothing bridges over the leaf.
# len 11.6 / nub 0.4 (was 10.0 / 0.5): in print pose the leaf stands vertical, so it bends ACROSS the layer lines;
# strain is held under 0.7% and both faces get a 45 deg root fillet (LEAF_RF) where the cavity ends
LEAF = dict(w=5.0, t=0.8, slit=0.6, len=11.6, relief=0.2, gap=0.6, nub=0.4)   # len on the 0.2 grid (print height)
LEAF_FREE = LEAF["len"] - EST.lockpin_back       # root to nub 9.1
LEAF_RF = 0.6                                    # root fillet (45 deg) on the gap face; the 0.2 relief face gets 0.2 x 0.6
LEAF_STRAIN_LIM = 0.7                            # % for a leaf whose long axis is vertical in print (bulk PETG ~1.5)
# mouse ears (coupon C5 lesson): two discs on the foot's bed face (its front end, y = PL_Y1), one each side, centred
# outside the fairing walls so they miss the plate, its 0.8 lead-in and the leaf slits. Print-only: they are added to
# the print-pose foot, never to the assembled one, and are snapped off and trimmed flush before fitting.
EAR = dict(dx=13.4, z=Z_WEB0 - 1.0, r=3.8, t=0.4)   # centre |x - FOOT_X|, centre z, radius, thickness (2 layers)
FOOT_MIN_BED_18 = 90.0                           # [DES] coupon rule: bed contact for a standing foot 18 tall


def bx(v): return v - 12.5
def by(u): return u + U0
def bz(w): return w + PCB_Z1


# ===================================================================== helpers
def box(x0, x1, y0, y1, z0, z1):
    return M.cube((x1 - x0, y1 - y0, z1 - z0)).translate((x0, y0, z0))


def rrect(x0, x1, y0, y1, r):
    w, h = x1 - x0, y1 - y0
    r = max(min(r, w / 2 - 0.01, h / 2 - 0.01), 0.01)
    return CS.square((w - 2 * r, h - 2 * r)).translate((x0 + r, y0 + r)).offset(r, JoinType.Round)


def slab(cs, z0, z1):
    return M.extrude(cs, z1 - z0).translate((0, 0, z0))


def union(ps):
    ps = [p for p in ps if p is not None and not p.is_empty()]
    return M.batch_boolean(ps, mf.OpType.Add) if ps else M()


def poly(pts):
    cs = CS([[tuple(map(float, p)) for p in pts]])
    if cs.area() <= 1e-9:
        cs = CS([[tuple(map(float, p)) for p in pts[::-1]]])
    return cs


def prism_y(pts_xz, y0, y1):
    T = np.array([[1, 0, 0, 0], [0, 0, -1, y1], [0, 1, 0, 0]], float)
    return M.extrude(poly(pts_xz), y1 - y0).transform(T)


def prism_x(pts_yz, x0, x1):
    T = np.array([[0, 0, 1, x0], [1, 0, 0, 0], [0, 1, 0, 0]], float)
    return M.extrude(poly(pts_yz), x1 - x0).transform(T)


def tear_inset(h, r):
    s = math.sqrt(0.5)
    if h >= r:
        return 0.0
    if h >= r * (1 - s):
        return r - math.sqrt(max(r * r - (r - h) ** 2, 0))
    return (1 - s) * r + (r * (1 - s) - h)


def plan(inset=0.0):
    return rrect(-W2 + inset, W2 - inset, inset, L - inset, max(R_PLAN - inset, 0.3))


def pk(d):
    cs = []
    for sx in (-1, 1):
        for (yc, r) in ((Y_IN0 + POCKET_R_REAR, POCKET_R_REAR), (Y_IN1 - POCKET_R_FRONT, POCKET_R_FRONT)):
            cs.append(CS.circle(max(r - d, 0.05), 64).translate((sx * (CAV_X - r), yc)))
    return CS.batch_hull(cs)


def block(inset0, z0, z1, r_bot, r_top):
    zs = sorted(set([z0, z1] + [z0 + r_bot * t for t in np.linspace(0, 1, 9)] +
                    [z1 - r_top * t for t in np.linspace(0, 1, 9)]))
    sl = []
    for z in zs:
        d = inset0 + max(tear_inset(z - z0, r_bot) if r_bot else 0, tear_inset(z1 - z, r_top) if r_top else 0)
        sl.append(slab(plan(d), z - E / 2, z + E / 2))
    return M.batch_hull(sl)


def tm(m):
    mm = m.to_mesh()
    return trimesh.Trimesh(np.asarray(mm.vert_properties)[:, :3], np.asarray(mm.tri_verts), process=False)


def vol(m):
    return 0.0 if m.is_empty() else m.volume()


# ===================================================================== antenna path
ANT_Z = 19.1          # bore centre (bore top 20.05: top skin over it 1.15)
BORE_R = 0.95         # 1.6 sleeve + 0.15 radial
MOUTH = 1.3           # 45 deg side-entry slot: the 1.6 sleeve snaps past it
V_DEG = 48.0          # teardrop V flank angle from horizontal
LANE_X = 13.6
CROSS_Y = U0 + 12.5     # 18.8 (board-relative: the cross run sits just in front of the PRG hinge band)
ANT_R = 5.0           # corner fillets (>= 4)
RIB_FLOOR = 15.9      # rib underside: clears the board top parts (15.7)
RAMP = 12.0           # cable rises from the plug height to ANT_Z over this length
RAMP_IN = 5.0         # ... and reaches ANT_Z this far inside the groove
FREE_MARGIN = 5.0     # free cable must exceed the plug-to-entrance distance (cover open 90 deg on its front edge) by this
GROOVE_S0 = None      # set below: the groove starts where leg B of the service loop turns into the left lane
GROOVE_EXTRA = 6.0    # groove runs this much past the nominal antenna tip
IPEX_XY = (bx(EST.ipex_uv[1]), by(EST.ipex_uv[0]))


def fillet_path(pts, r, step=0.25):
    pts = [np.array(p, float) for p in pts]
    rr = list(r) if isinstance(r, (list, tuple)) else [r] * (len(pts) - 2)
    out = [pts[0]]
    for i in range(1, len(pts) - 1):
        r = rr[i - 1]
        P = pts[i]
        a = (P - pts[i - 1]); a /= np.linalg.norm(a)
        b = (pts[i + 1] - P); b /= np.linalg.norm(b)
        phi = math.acos(float(np.clip(a @ b, -1, 1)))
        t = r * math.tan(phi / 2)
        s, e = P - a * t, P + b * t
        cr = a[0] * b[1] - a[1] * b[0]
        n = np.sign(cr) * np.array([-a[1], a[0]])
        c = s + n * r
        a0 = math.atan2(s[1] - c[1], s[0] - c[0])
        out.append(s)
        for k in range(1, 40):
            ang = a0 + np.sign(cr) * phi * k / 40
            out.append(c + r * np.array([math.cos(ang), math.sin(ang)]))
        out.append(e)
    out.append(pts[-1])
    out = np.array(out)
    seg = np.linalg.norm(np.diff(out, axis=0), axis=1)
    cum = np.concatenate([[0], np.cumsum(seg)])
    ss = np.arange(0, cum[-1], step)
    return np.column_stack([np.interp(ss, cum, out[:, 0]), np.interp(ss, cum, out[:, 1])]), ss


ANT_TOTAL = EST.ant_len + GROOVE_EXTRA
# the U.FL plug is snapped with its cable pointing RIGHT (+x). Leg A runs right at the plug, a U-turn (r LOOP_R) brings
# it back as leg B along the nose bay, then the r5 corner turns it into the left lane where the groove starts.
PLUG_OFF = 1.0       # path starts this far from the IPEX centre (the plug body)
_path_xy, _path_s = fillet_path([(IPEX_XY[0] + PLUG_OFF, LOOP_YA), (LOOP_X, LOOP_YA), (LOOP_X, LOOP_YB), (-LANE_X, LOOP_YB),
                                 (-LANE_X, CROSS_Y), (LANE_X, CROSS_Y), (LANE_X, 200.0)],
                                [LOOP_R, LOOP_R, ANT_R, ANT_R, ANT_R])
_keep = _path_s <= ANT_TOTAL
PATH_XY, PATH_S = _path_xy[_keep], _path_s[_keep]
PLUG_Z = bz(EST.cable_z)
GROOVE_S0 = float(PATH_S[np.argmax(PATH_XY[:, 0] < -LANE_X + ANT_R)]) - 0.25        # start of the front-left corner
S_RAMP0 = GROOVE_S0 + RAMP_IN - RAMP              # the loop lies flat at the plug height, then rises into the groove


def path_z(s):
    s = np.asarray(s, float)
    t = np.clip((s - S_RAMP0) / RAMP, 0, 1)
    return PLUG_Z + (ANT_Z - PLUG_Z) * (1 - np.cos(math.pi * t)) / 2


PATH_Z = path_z(PATH_S)


def path_normals(xy):
    t = np.gradient(xy, axis=0)
    t /= np.linalg.norm(t, axis=1)[:, None]
    return np.column_stack([-t[:, 1], t[:, 0]])     # left normal = inward (path runs counter-clockwise)


PATH_N = path_normals(PATH_XY)


def deep_lane(p):
    return abs(p[0]) >= 13.0


def sweep_convex(prof_fn, idx):
    """prof_fn(i) -> list of (l, z) convex profile at path point i (same vertex count everywhere). Lofted as ONE
    closed mesh (rings joined by quads, fan caps), so there are no internal seams that could pinch."""
    rings = []
    for k in idx:
        pr = np.asarray(prof_fn(k), float)
        a2 = 0.5 * np.sum(pr[:, 0] * np.roll(pr[:, 1], -1) - np.roll(pr[:, 0], -1) * pr[:, 1])
        if a2 < 0:
            pr = pr[::-1]
        rings.append(np.column_stack([PATH_XY[k][0] + pr[:, 0] * PATH_N[k][0], PATH_XY[k][1] + pr[:, 0] * PATH_N[k][1], pr[:, 1]]))
    n, R = len(rings[0]), len(rings)
    V = np.vstack(rings)
    F = []
    for r in range(R - 1):
        for j in range(n):
            a, b = r * n + j, r * n + (j + 1) % n
            c, d = a + n, b + n
            F += [(a, b, d), (a, d, c)]
    for j in range(1, n - 1):
        F.append((0, j + 1, j))
        o = (R - 1) * n
        F.append((o, o + j, o + j + 1))
    F = np.array(F, np.uint32)
    m = M(mf.Mesh(vert_properties=np.ascontiguousarray(V, np.float32), tri_verts=F))
    if m.is_empty() or m.volume() < 0 or m.status() != mf.Error.NoError:
        m = M(mf.Mesh(vert_properties=np.ascontiguousarray(V, np.float32), tri_verts=np.ascontiguousarray(F[:, ::-1])))
    return m


def rib_prof(k):
    l0, z0 = -1.95, min(RIB_FLOOR, PATH_Z[k] - BORE_R * math.sqrt(2) - 1.05)
    l1 = BORE_R + 1.05                               # 1.05 wall inboard of the bore (the mouth is cut through it)
    return [(l0, z0), (l1, z0), (l1, TOP_IN + E), (l0, TOP_IN + E)]


def bore_prof(k):
    """teardrop: 1.9 circle with a 45 deg V underneath (prints as a peaked roof with the cover top face down)."""
    zc = PATH_Z[k]
    al = math.radians(V_DEG)               # flanks 48 deg from horizontal = 42 deg overhang: margin for the rising ramp
    pts = [(BORE_R * math.cos(a), zc + BORE_R * math.sin(a)) for a in np.linspace(-(math.pi / 2 - al), math.pi + (math.pi / 2 - al), 28)]
    return pts + [(0.0, zc - BORE_R / math.cos(al))]


STRAIGHT = np.minimum(np.abs(PATH_N[:, 0]), np.abs(PATH_N[:, 1])) < 1e-3   # straight runs get the snap-in mouth


def band_prof(k):
    # lower-outer edge is the 45 deg tangent to the bore (smooth), upper-inner edge 1.3 above it, running down-inward
    s = math.sqrt(0.5)
    T = np.array([-BORE_R * s, ANT_Z - BORE_R * s])
    d = np.array([s, -s])
    n = np.array([s, s])
    T = T - n * 0.01                      # 0.01 past the teardrop's V flank so the two cuts never share a face
    p0, p1 = T - d * 0.5, T + d * 4.0
    return [tuple(p0), tuple(p1), tuple(p1 + n * MOUTH), tuple(p0 + n * MOUTH)]


def notch_prof(k):
    """straight runs: below the mouth's lower edge the lip stops at l 1.05 (so its tip is >= 0.8 thick)."""
    zc = PATH_Z[k]
    low = lambda l: zc - BORE_R * math.sqrt(2) - l
    return [(1.05, RIB_FLOOR - 0.5), (BORE_R + 1.2, RIB_FLOOR - 0.5), (BORE_R + 1.2, low(BORE_R + 1.2)), (1.05, low(1.05))]


def entry_flare():
    """45 deg funnel at the groove entrance, on the cable's own (rising) axis: the tip threads in without a ledge."""
    i0 = int(np.argmax(PATH_S >= GROOVE_S0))
    i1 = min(i0 + 4, len(PATH_S) - 1)
    a = np.array([PATH_XY[i0][0], PATH_XY[i0][1], PATH_Z[i0]])
    b = np.array([PATH_XY[i1][0], PATH_XY[i1][1], PATH_Z[i1]])
    u = (b - a) / np.linalg.norm(b - a)
    z = np.array([0.0, 0.0, 1.0])
    v = np.cross(z, u); sn = np.linalg.norm(v); cs = float(z @ u)
    if sn < 1e-9:
        R = np.eye(3)
    else:
        k = v / sn
        K = np.array([[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]])
        R = np.eye(3) + sn * K + (1 - cs) * K @ K
    h = 1.8
    cone = M.cylinder(h, BORE_R + h, BORE_R, circular_segments=48).translate((0, 0, -h * 0.5))
    T = np.hstack([R, (a + u * 0.4)[:, None]])          # wide mouth at the rib face, narrows to the bore 1.3 in
    return cone.transform(T)


def antenna_solid(s0=0.0, s1=None):
    pts = np.column_stack([PATH_XY, PATH_Z])
    sel = (PATH_S <= (EST.ant_len if s1 is None else s1)) & (PATH_S >= s0)
    pts = pts[sel][::2]
    out = []
    r = EST.ant_d / 2
    for a, b in zip(pts[:-1], pts[1:]):
        out.append(M.batch_hull([M.sphere(r, 16).translate(tuple(a)), M.sphere(r, 16).translate(tuple(b))]))
    return union(out)


# ===================================================================== board features
SC_Y = (by(EST.bracket_u[0]) + by(EST.bracket_u[1])) / 2
PRG_XY = (bx(EST.prg_v), by(EST.btn_u))          # (8.0, 6.8)
RST_XY = (bx(EST.rst_v), by(EST.btn_u))          # (-8.0, 6.8)
SW_TOP = bz(EST.btn_top)                          # 16.5
NUB_GAP = 0.3                                     # puts the nub tips on a whole layer (z' 4.4)
FLEX_T = 0.8                                      # PRG disc and RST tile skin (flexure)
# PRG: flush round disc, C-slit, hinge toward the screen
PRG = dict(cx=PRG_XY[0], cy=PRG_XY[1] + 2.0, r=3.4, slit=0.6, hinge_deg=90.0, band=2.0, nub_d=2.0)
# RST: square tile in a 0.8 recess, hinged on its OUTBOARD (-x) edge, one-layer sacrificial skin under the slit.
# (It used to hinge at the front, which put the light-pipe hole in the hinge root.) The recess floor runs on 'apron'
# in front of the tile as solid floor, so the light pipe sits in thick boss ~2 clear of every slit.
RST = dict(x0=RST_XY[0] - 2.3, x1=RST_XY[0] + 2.3, y0=RST_XY[1] - 2.4, y1=RST_XY[1] + 3.0, r=0.8, slit=0.6,
           margin=0.4, depth=0.8, band=2.8, membrane=0.2, nub_d=1.8, apron=2.4)
RST["xr"] = RST["x0"] - RST["band"]               # hinge root line (-13.1), inside the +-15.2 inner wall
# charge LED: a clear 1.75 filament offcut is pushed into a vertical hole in the solid apron in front of the RST tile.
# Its foot sits flush with the tube's bottom end, 0.95 over the LED; its top is cut flush with the RST recess floor, so
# the glow shows on the recess's front 45 deg wall, which faces the shooter behind the camera. The top 0.8 of the hole
# (in the skin) is a 1.70 press-fit neck that holds it in z (plus a drop of CA). (A plain hole cannot work: the LED is
# 5.4 below the top and the RST switch, nub and boss block every 60-80 deg sight line.)
LIGHT_PIPE = dict(x=-8.3, y=U0 + 9.3, hole_d=EST.pipe_d + 0.1, neck_d=EST.pipe_d - 0.05, tube_r=1.85, z0=None)
RST_FLOOR = (RST["xr"] - RST["margin"], RST["x1"] + RST["slit"] + RST["margin"],
             RST["y0"] - RST["slit"] - RST["margin"], RST["y1"] + RST["apron"] + RST["margin"])
LIGHT_PIPE["z0"] = PCB_Z1 + EST.top_parts_h + 0.05     # foot of the pipe, just above the top-parts keep-out (15.75)
RST_BOSS_Z = TOP_IN - 1.0                        # 18.8 boss under the recess (skin under the floor 1.6)
# board clamps: two at the USB end (the left one sits just in front of the RST tile's slits, clear of its outboard hinge), one at the left of the nose tip, one on the right pad strip just behind the bracket's
# front end (the old right tip clamp sat inside the antenna service loop)
POSTS = [(-12.4, -11.4, by(6.6), by(7.6)), (11.4, 12.4, by(1.6), by(2.6)),
         (bx(7.3), bx(8.5), by(51.1), by(52.1)), (10.4, 11.4, by(45.0), by(46.0))]
PORT = dict(w=EST.usb_env[0] + 0.5, top=None, r=1.25, sur=0.5)
USB_ZC = bz((EST.usb_z[0] + EST.usb_z[1]) / 2)   # 16.2
OVM_Z = (USB_ZC - EST.usb_env[1] / 2, USB_ZC + EST.usb_env[1] / 2)
PORT["top"] = TOP_IN                             # 19.8: 0.35 over the overmold, roof 1.4 (whole layers)
REC_FACE_Y = by(-EST.usb_proud)                  # 5.6


def board_outline():
    n0 = EST.nose_u
    c = EST.usb_chamfer
    return poly([(bx(0), by(c)), (bx(c), by(0)), (bx(25 - c), by(0)), (bx(25), by(c)), (bx(25), by(n0)),
                 (bx(EST.tip_v[1]), by(52)), (bx(EST.tip_v[0]), by(52)), (bx(0), by(n0))])


# ===================================================================== contents / keep-outs
def contents():
    c = {}
    c["pcb"] = slab(board_outline(), PCB_Z0, PCB_Z1)
    bu, bv = EST.bracket_u, EST.bracket_v
    c["bracket"] = slab(rrect(bx(bv[0]), bx(bv[1]), by(bu[0]), by(bu[1]), EST.bracket_r), PCB_Z1, BRK_TOP)
    c["screen"] = slab(rrect(bx(EST.glass_v[0]), bx(EST.glass_v[1]), by(EST.glass_u[0]), by(EST.glass_u[1]), 0.6), BRK_TOP - 0.3, BRK_TOP - 0.02)
    c["usb"] = slab(rrect(-EST.usb_w / 2, EST.usb_w / 2, by(-EST.usb_proud), by(-EST.usb_proud + EST.usb_len), 1.2),
                    bz(EST.usb_z[0]), bz(EST.usb_z[1]))
    c["sockets"] = box(bx(EST.sock_v[0]), bx(EST.sock_v[1]), by(EST.sock_u[0]), by(EST.sock_u[1]),
                       PCB_Z0 - EST.sock_drop, PCB_Z0)
    bo = board_outline()
    under = slab(bo, PCB_Z0 - EST.under_h, PCB_Z0) - box(-20, 20, by(EST.nose_free_u), 80, 0, 30) \
        - box(-20, -12.5 + EST.pad_strip, -10, 80, 0, 30) - box(12.5 - EST.pad_strip, 20, -10, 80, 0, 30)
    c["underside"] = under
    tops = slab(bo, PCB_Z1, PCB_Z1 + EST.top_parts_h) - box(-20, 20, by(EST.bracket_u[0]), 80, 0, 30) - box(-20, -12.5 + EST.pad_strip, -10, 80, 0, 30) \
        - box(12.5 - EST.pad_strip, 20, -10, 80, 0, 30)
    c["top_parts"] = tops
    bu_, bv_ = EST.btn_body
    c["switches"] = union([box(x - bv_ / 2, x + bv_ / 2, y - bu_ / 2, y + bu_ / 2, PCB_Z1, SW_TOP) for (x, y) in (PRG_XY, RST_XY)])
    c["leds"] = box(bx(EST.led_v[0]), bx(EST.led_v[1]), by(EST.led_u[0]), by(EST.led_u[1]), PCB_Z1, bz(EST.led_h))
    c["ipex"] = M.cylinder(EST.ipex_h + EST.ipex_relief, EST.ipex_d / 2 + EST.ipex_relief).translate((IPEX_XY[0], IPEX_XY[1], PCB_Z1))
    c["battery"] = slab(rrect(-CELL_W / 2, CELL_W / 2, CELL_Y0, CELL_Y1, 1.0), BATT_Z0, BATT_Z1)
    # lead bay: full height behind the socket mouths, only under the sockets in front of them (no content overlap)
    c["lead_bay"] = box(-10.5, 10.5, Y_IN0 + 0.05, by(0.0), FLOOR, PCB_Z0 - EST.under_h - 0.01) + \
        box(-10.5, 10.5, by(0.0) - E, CELL_Y0, FLOOR, PCB_Z0 - EST.sock_drop - 0.01)
    # mated SH1.25 plugs (both sockets, as if both were used) standing proud of the mouths, then the wires leaving along -u
    # and turning 90 deg down into the lead bay
    sx0, sx1 = bx(EST.sock_v[0]), bx(EST.sock_v[1])
    c["sh_plug"] = box(sx0, sx1, by(0.0) - EST.plug_proud, by(0.0), PCB_Z0 - EST.sock_drop, PCB_Z0)
    c["sh_wire_bend"] = box(sx0, sx1, by(0.0) - EST.plug_proud - EST.plug_bend, by(0.0) - EST.plug_proud + E,
                            PCB_Z0 - EST.sock_drop - EST.plug_bend, PCB_Z0)
    w, h, ln, r = EST.usb_env
    c["usb_overmold"] = M.extrude(rrect(-w / 2, w / 2, OVM_Z[0], OVM_Z[1], r), ln).transform(
        np.array([[1, 0, 0, 0], [0, 0, -1, REC_FACE_Y], [0, 1, 0, 0]], float))
    c["antenna"] = antenna_solid()
    lp = LIGHT_PIPE
    c["light_pipe"] = M.cylinder(H - RST["depth"] - lp["z0"], EST.pipe_d / 2, circular_segments=32).translate((lp["x"], lp["y"], lp["z0"]))
    return c


# ===================================================================== base (black)
def snap_arm(s, y, part="all"):
    """part 'body' is built 0.2 proud on the inside and trimmed flush by the pocket cut (no coplanar unions);
    part 'bump' is added after the cut."""
    xi, xa, w = CAV_X, CAV_X + ARM_T, ARM_W / 2
    if part == "body":
        xi = CAV_X - 0.2
    arm = M.batch_hull([box(xi, xa, y - w, y + w, SPLIT - E, ARM_TOP - ARM_T),
                        box(xi, xi + E, y - w, y + w, ARM_TOP - E, ARM_TOP)])
    flare = M.batch_hull([box(xi, xa + ROOT_F, y - w - ROOT_F, y + w + ROOT_F, SPLIT - E, SPLIT),
                          box(xi, xa, y - w, y + w, SPLIT + ROOT_F, SPLIT + ROOT_F + E)])
    z0, z1, z2, z3 = BUMP_Z
    bump = prism_y([(xi + E, z0), (xi - BUMP, z1), (xi - BUMP, z2), (xi + E, z3)], y - w + 0.4, y + w - 0.4)
    a = {"body": arm + flare, "bump": bump}.get(part, arm + flare + bump)
    return a if s > 0 else a.mirror((1, 0, 0))


def key_shape(clr=0.0, extra=0.0):
    hb, h = 1.1 + clr, 0.8 + extra
    ht = hb - h
    longr = prism_y([(FOOT_X - hb, -E - 0.5), (FOOT_X + hb, -E - 0.5), (FOOT_X + hb, 0), (FOOT_X + ht, h), (FOOT_X - ht, h),
                     (FOOT_X - hb, 0)], KEY_LONG_Y0, PL_Y1)
    cy = KEY_CROSS_Y
    cross = prism_x([(cy - hb, -E - 0.5), (cy + hb, -E - 0.5), (cy + hb, 0), (cy + ht, h), (cy - ht, h), (cy - hb, 0)],
                    FOOT_X - 8.0, FOOT_X + 8.0)
    return longr + cross


def ledge_45(fp, top_z, thick=1.0):
    """convex footprint fp hung off the front or rear wall: flat top at top_z, 45 deg underside to the wall."""
    x0, y0, x1, y1 = fp.bounds()
    if y1 > L / 2:   # front: hangs off the cell stop wall
        # (wall starts 0.02 INSIDE the stop wall, never on its face: a 0.01 step there left a sliver the STL weld broke)
        wall = slab(rrect(x0, x1, STOP_Y0 + 0.02, STOP_Y0 + 0.3, 0.01), top_z - thick - (STOP_Y0 + 0.02 - y0), top_z)
    else:
        wall = slab(rrect(x0, x1, Y_IN0 - 0.3, Y_IN0 + E, 0.01), top_z - thick - (y1 - Y_IN0), top_z)
    return M.batch_hull([slab(fp, top_z - thick, top_z), wall])


def fence_cs(side):
    """0.8 x 3.0 fence 0.25 outside the board's nose chamfer (side +1 right, -1 left)."""
    p0 = np.array([bx(EST.tip_v[1]) if side > 0 else bx(EST.tip_v[0]), by(52.0)])
    q = np.array([bx(25.0) if side > 0 else bx(0.0), by(EST.nose_u)])
    t = (q - p0) / np.linalg.norm(q - p0)
    n = np.array([t[1], -t[0]]) if side > 0 else np.array([-t[1], t[0]])
    if n[1] < 0:
        n = -n
    pts = [p0 + t * a + n * b for (a, b) in ((1.2, 0.25), (4.2, 0.25), (4.2, 1.25), (1.2, 1.25))]
    return poly(pts)


# coin slot (coupon C1 lesson): 12 wide along the seam, 2.2 tall, cut into the BASE only, just under the seam, depth
# measured from the outer skin (the cover face). The cover is left whole: its seam face is what the coin levers on.
# The rear face has only 5.0 of flat between the 12.5 USB notch and the R8 plan corner, so one slot sits in each SIDE
# wall, in the rear half, between the two snaps (the y 15 snap clearance ends at y 19.1).
PRY = (12.0, 2.2, 1.35)                         # [DES] width along y, height below the seam, depth from the cover face
PRY_Y = (20.0, 32.0)                            # [DES] slot ends along y (both sides)
COIN = (22.5, 1.85)                             # [DS] test coin (UK 2p-ish), as in test_coupons
COIN_REACH = 0.8                                # [DES] coin rim must get this far past the base skin touching nothing


def build_base():
    # tongue and arms are built solid and the pocket is cut once through all of them, so no faces coincide
    outer = block(PROUD, 0.0, SPLIT, R_BOT, 0.0)
    pocket = slab(pk(0.0), FLOOR, ARM_TOP + 1)
    tongue = slab(plan(WALL - TONGUE_T), SPLIT - E, SPLIT + TONGUE_H)
    tongue = tongue - box(-30, 30, -1, Y_IN0 + 0.6, SPLIT - 1, SPLIT + 3)
    for y in SNAP_YS:
        for s in (-1, 1):
            tongue = tongue - box(min(s * 15, s * 22), max(s * 15, s * 22), y - ARM_W / 2 - 0.6 - ROOT_F,
                                  y + ARM_W / 2 + 0.6 + ROOT_F, SPLIT - 1, SPLIT + 3)
    base = union([outer, tongue] + [snap_arm(s, y, "body") for s in (-1, 1) for y in SNAP_YS]) - pocket
    base = base + union([snap_arm(s, y, "bump") for s in (-1, 1) for y in SNAP_YS])
    # board rests: nose strip under the free tip, two rear ledges under the pad strips, fences that locate it sideways
    rests = [ledge_45(rrect(-5.5, 5.5, by(EST.nose_free_u), STOP_Y0 + E, 0.01), PCB_Z0)]
    for s in (-1, 1):
        fc = fence_cs(s)
        rests.append(ledge_45(fc, PCB_Z0))
        rests.append(slab(fc, PCB_Z0 - E, PCB_Z0 + 1.2))
    for (x0, x1) in ((-13.55, -10.9), (10.9, 13.55)):
        rests.append(ledge_45(rrect(x0, x1, Y_IN0 - E, by(3.0), 0.01), PCB_Z0, thick=0.8))
    for (x0, x1) in ((-13.55, -12.75), (12.75, 13.55)):
        rests.append(box(x0, x1, by(1.6), by(3.0), PCB_Z0 - E, PCB_Z0 + 1.2))
    # nose: cell stop wall, then a deck that is the floor of the antenna service-loop bay (bridges 7 from the stop wall
    # to the front wall in print pose). Over |x| <= BAY_X - 0.15 the deck rises to the tongue top, where the cover has no
    # inner lip, so nothing of the cover ever closes down onto it.
    pkf = pk(-0.05)                  # 0.05 into the walls: face-to-face contact with them left weld pinches
    deck_lo = SPLIT - 1.2
    rests.append(slab(pkf ^ CS.square((50, STOP_T)).translate((-25, STOP_Y0)), FLOOR - E, deck_lo + E))
    rests.append(slab(pkf ^ CS.square((50, Y_IN1 + E - STOP_Y0)).translate((-25, STOP_Y0)), deck_lo, SPLIT + E))
    # (sunk 0.3 into the deck and 0.37 into the front wall, its rear face 0.013 off the deck's: thin overlaps left weld pinches)
    rests.append(box(-BAY_X + 0.15, BAY_X - 0.15, STOP_Y0 + 0.013, Y_IN1 + 0.37, SPLIT - 0.3, SPLIT + TONGUE_H))
    # shelf beside the board's right nose chamfer: the loop's U-turn passes there, so the cell always has >= 1 of
    # plastic (or the board) under the antenna
    # plus a lip under the board edge (in the free nose strip, where the board also rests on it), so no straight line
    # finds the 0.25 slot between board and shelf. Both sides, mirrored.
    p0 = np.array([bx(EST.tip_v[1]), by(52.0)]); q = np.array([bx(25.0), by(EST.nose_u)])
    t = (q - p0) / np.linalg.norm(q - p0); n = np.array([-t[1], t[0]])
    n = n if n[1] > 0 else -n
    a, b = p0 + n * 0.25, q + n * 0.25
    at = lambda yy: p0 + (q - p0) * (by(52.0) - yy) / (by(52.0) - by(EST.nose_u))
    y_lip = by(EST.nose_free_u)
    for sx in (1, -1):
        mx = lambda pt: (sx * float(pt[0]), float(pt[1]))
        rests.append(ledge_45(poly([mx(b), mx((BAY_X - 0.15, b[1])), mx((BAY_X - 0.15, STOP_Y0 + E)), mx((a[0], STOP_Y0 + E)), mx(a)]),
                              SPLIT + TONGUE_H))
        lip = poly([mx(at(y_lip - 1.0) - n * 1.0), mx(at(y_lip - 1.0) + n * 0.3), mx(at(STOP_Y0 + E) + n * 0.3), mx(at(STOP_Y0 + E) - n * 1.0)])
        rests.append(ledge_45(lip ^ CS.square((40, 10)).translate((-20, y_lip + 0.05)), PCB_Z0))
    base = base + union(rests)
    # vent through the foot of the cell stop wall: the void under the nose deck is closed on every other side (the deck
    # and the stop wall sit 0.05 into the walls), so it gets a 1.2 x 1.6 window to the cell pocket. Its roof is a 1.2 bridge.
    base = base - box(-0.6, 0.6, STOP_Y0 - 0.1, STOP_Y0 + STOP_T + 0.1, FLOOR - E, FLOOR + 1.6)
    # coin slots: 12.0 x 2.2, 1.35 deep from the cover face (1.05 into the base, leaving a 1.05 wall to the pocket),
    # open to the seam, one in each side wall in the rear half. Twisting a coin there lifts the rear first and the
    # cover hinges on its front edge, away from the IPEX.
    for sx in (-1, 1):
        xin = W2 - PRY[2]
        base = base - box(min(sx * xin, sx * (W2 + 1)), max(sx * xin, sx * (W2 + 1)), PRY_Y[0], PRY_Y[1], SPLIT - PRY[1], SPLIT + E)
    for (sx, sy) in SCREWS:
        base = base - M.cylinder(FLOOR + 1, 1.15).translate((sx, sy, -0.5)) \
                    - M.cylinder(1.5, 2.2).translate((sx, sy, FLOOR - 1.0))
    base = base - key_shape(clr=0.1, extra=0.2)
    return base


# ===================================================================== cover (silver)
def window_cs(grow=0.0):
    """OLED bracket outline + 0.3 per side."""
    bu, bv = EST.bracket_u, EST.bracket_v
    g = 0.3 + grow
    return rrect(bx(bv[0]) - g, bx(bv[1]) + g, by(bu[0]) - g, by(bu[1]) + g, EST.bracket_r + g)


WIN_Z = BRK_TOP + BRK_CLR - 0.1                   # glass-level edge of the window frame (20.1)


def window_cut():
    dd = H - WIN_Z
    return M.batch_hull([slab(window_cs(), WIN_Z, WIN_Z + 0.11), slab(window_cs(dd), H - E, H + 0.5)])


def bore_solid():
    """the antenna bore (cut) on its own, for wall checks."""
    par = (int(np.argmax(PATH_S >= GROOVE_S0)) + 1) % 2
    bi = [i for i in range(len(PATH_S)) if PATH_S[i] >= GROOVE_S0 - 1.0 and i % 2 == par]
    return sweep_convex(bore_prof, bi)


def btn_slit():
    b = PRG
    ring = CS.circle(b["r"] + b["slit"], 128) - CS.circle(b["r"], 128)
    a = math.radians(b["hinge_deg"] / 2)
    wedge = poly([(0, 0), (9 * math.sin(a), 9 * math.cos(a)), (0, 10), (-9 * math.sin(a), 9 * math.cos(a))])
    return (ring - wedge).translate((b["cx"], b["cy"]))


def prg_thin_cs():
    b = PRG
    hw = (b["r"] + b["slit"]) * math.sin(math.radians(b["hinge_deg"] / 2))
    disc = CS.circle(b["r"] + b["slit"] + 0.08, 128).translate((b["cx"], b["cy"]))
    band = rrect(b["cx"] - hw, b["cx"] + hw, b["cy"], b["cy"] + b["r"] + b["band"], 0.5)
    return disc + band


def rst_tile_cs():
    t = RST
    return rrect(t["x0"], t["x1"], t["y0"], t["y1"] + 1.0, t["r"]) ^ rrect(t["x0"], t["x1"], t["y0"], t["y1"] + 5, t["r"])


def rst_slit_cs():
    t = RST
    xr = t["xr"]                                   # slit runs along the band too, so the band is a true cantilever
    outer = rrect(xr - 2, t["x1"] + t["slit"], t["y0"] - t["slit"], t["y1"] + t["slit"], t["r"] + t["slit"])
    inner = rrect(xr - 4, t["x1"], t["y0"], t["y1"], t["r"])
    return outer - inner - CS.square((10, 60)).translate((xr + 0.001 - 10, -20))


def build_cover():
    outer = block(0.0, SPLIT, H, 0.0, R_TOP)
    # interior: the pocket outline inset 1.3 (a uniform 1.2 wall inside the tongue groove, concentric corners),
    # sides held at +-15.2 so the snap slots keep their inner wall
    up = slab(pk(GROOVE_CLR + 1.2) ^ CS.square((30.4, 100)).translate((-15.2, -10)), SPLIT - 1, TOP_IN)
    brk = slab(rrect(bx(EST.bracket_v[0]) - BRK_LAT, bx(EST.bracket_v[1]) + BRK_LAT, by(EST.bracket_u[0]) - BRK_LAT,
                     by(EST.bracket_u[1]) + BRK_LAT, EST.bracket_r + BRK_LAT), PCB_Z1, BRK_TOP + BRK_CLR)
    # nose bay: no inner lip over |x| <= BAY_X, so the cover's front face sits 1.6 clear of the loose service loop and
    # comes down beside the base deck, never onto it; 1.0 x 45 lead on its bottom inner edge pushes a stray cable inward
    yb = Y_IN1 - 0.1
    bay = slab(rrect(-BAY_X, BAY_X, Y_IN1 - 6.0, yb, 0.01), SPLIT - 1, TOP_IN) + \
        prism_x([(yb - E, SPLIT + TONGUE_H + 0.2 - E), (yb + 1.0, SPLIT + TONGUE_H + 0.2 - E), (yb - E, SPLIT + TONGUE_H + 1.2)], -BAY_X, BAY_X)
    cover = outer - up - brk - bay
    # ---- solids hung from the top skin
    adds = [box(x0, x1, y0, y1, PCB_Z1, TOP_IN + E) for (x0, x1, y0, y1) in POSTS]
    f = RST_FLOOR
    adds.append(slab(rrect(f[0] - 0.5 - RST["depth"], f[1] + 0.5 + RST["depth"], f[2] - 0.5 - RST["depth"], f[3] + 0.5 + RST["depth"], 1.5),
                     RST_BOSS_Z, TOP_IN + E))
    gi = [i for i in range(len(PATH_S)) if PATH_S[i] >= GROOVE_S0]
    rib = sweep_convex(rib_prof, gi[::2]) - brk
    adds.append(rib)
    lp = LIGHT_PIPE
    adds.append(M.cylinder(TOP_IN + E - lp["z0"], lp["tube_r"], circular_segments=48).translate((lp["x"], lp["y"], lp["z0"])))
    cover = cover + union(adds)
    # ---- tongue groove (0.1 clearance each side, 0.2 above); none at the USB end
    groove = slab(plan(WALL - TONGUE_T - GROOVE_CLR) - pk(GROOVE_CLR), SPLIT - 1, SPLIT + TONGUE_H + 0.2)
    groove = groove - box(-30, 30, -1, Y_IN0 + 0.5, SPLIT - 2, SPLIT + 3)
    cuts = [groove]
    # ---- snap slots: relief, 45 deg ceiling, root-fillet clearance, catch recess, 45 deg lead-in at the mouth
    for y in SNAP_YS:
        for s in (-1, 1):
            xi = CAV_X - GROOVE_CLR
            xo = CAV_X + ARM_T + ARM_RELIEF
            w = ARM_W / 2 + 0.4
            c45 = ARM_TOP + CAV_X + 0.3 + 0.2
            slot = M.batch_hull([box(xi, xo, y - w, y + w, SPLIT - 1, c45 - xo),
                                 box(xi, xi + E, y - w, y + w, SPLIT - 1, c45 - xi)])
            slot = slot + M.batch_hull([box(xi, xo, y - w - ROOT_F - 0.15, y + w + ROOT_F + 0.15, SPLIT - 1, SPLIT),
                                        box(xi, xo, y - w, y + w, SPLIT + ROOT_F + 0.2, SPLIT + ROOT_F + 0.2 + E)])
            z0, z1, z2, z3 = BUMP_Z
            d = BUMP + 0.05
            rec = prism_y([(xi + E, z0), (xi - d, z0 + d), (xi - d, z3 + 0.2 - d), (xi + E, z3 + 0.2)],
                          y - ARM_W / 2 + 0.2, y + ARM_W / 2 - 0.2)
            lead = prism_y([(xi + E, SPLIT - 1), (xi - BUMP - 0.1, SPLIT - 1), (xi - BUMP - 0.1, SPLIT), (xi + E, SPLIT + BUMP + 0.1)],
                           y - ARM_W / 2, y + ARM_W / 2)
            cut = slot + rec + lead
            cuts.append(cut if s > 0 else cut.mirror((1, 0, 0)))
    # ---- USB-C: U-notch open at the seam, the 12 x 6.5 overmold seats against the receptacle; 0.5 x 45 surround
    pw, pt, pr, su = PORT["w"], PORT["top"], PORT["r"], PORT["sur"]
    notch = M.extrude(rrect(-pw / 2, pw / 2, SPLIT - 3, pt, pr), REC_FACE_Y + 2).transform(
        np.array([[1, 0, 0, 0], [0, 0, -1, REC_FACE_Y + 1], [0, 1, 0, 0]], float))
    sur = M.batch_hull([M.extrude(rrect(-pw / 2 - su, pw / 2 + su, SPLIT - 3, pt + su, pr + su), 0.01).transform(
                            np.array([[1, 0, 0, 0], [0, 0, -1, 0.0], [0, 1, 0, 0]], float)).translate((0, -0.5, 0)),
                        M.extrude(rrect(-pw / 2, pw / 2, SPLIT - 3, pt, pr), 0.01).transform(
                            np.array([[1, 0, 0, 0], [0, 0, -1, su + 0.01], [0, 1, 0, 0]], float))])
    cuts += [notch, sur]
    # ---- screen window: the whole OLED bracket + 0.3 per side shows, 45 deg frame above it
    cuts.append(window_cut())
    # ---- PRG: flush disc, C-slit, thinned to 0.8 from inside (disc + hinge band), chamfer ring, dimple
    b = PRG
    ro = b["r"] + b["slit"]
    cuts.append(slab(btn_slit(), TOP_IN - 0.5, H + 1))
    cuts.append(slab(prg_thin_cs(), TOP_IN - 1, H - FLEX_T))
    # (radii kept a few hundredths apart so no two cut surfaces coincide)
    # cones are turned half a facet so their vertices never land on the slit's 128-gon
    # both chamfer rings stop short of the hinge: a (hinge_deg + 10) wedge is kept out, so the hinge band stays a full
    # FLEX_T from the slit ends out to the band root (no V notch on its tension face)
    tw = (0, 0, 360 / 256)
    a = math.radians((b["hinge_deg"] + 10) / 2)
    keep = slab(poly([(0, 0), (9 * math.sin(a), 9 * math.cos(a)), (0, 10), (-9 * math.sin(a), 9 * math.cos(a))]), -2, 2)
    ring1 = (M.cylinder(0.43, ro - 0.02, ro + 0.41, 128) - M.cylinder(1, ro - 0.03, circular_segments=128).translate((0, 0, -0.1))).rotate(tw)
    ring2 = (M.cylinder(0.2 + E, b["r"] - 0.2, b["r"] + E, 128) - M.cylinder(1, b["r"] - 0.17, circular_segments=128).translate((0, 0, -0.3))).rotate(tw)
    cuts.append((ring1 - keep).translate((b["cx"], b["cy"], H - 0.42)))
    cuts.append((ring2 - keep).translate((b["cx"], b["cy"], H - 0.2)))
    cuts.append(M.cylinder(0.4 + E, 0.8, 1.2 + E, 64).translate((b["cx"], b["cy"], H - 0.4)))
    # ---- RST: 0.8 recess (45 deg walls), slit from inside up to a one-layer sacrificial skin, tile thinned to 0.8
    t = RST
    fl = rrect(f[0], f[1], f[2], f[3], 1.0)
    fl2 = rrect(f[0] - t["depth"], f[1] + t["depth"], f[2] - t["depth"], f[3] + t["depth"], 1.0 + t["depth"])
    zf = H - t["depth"]
    cuts.append(M.batch_hull([slab(fl, zf, zf + E), slab(fl2, H - E, H + 0.5)]))
    cuts.append(slab(rst_slit_cs(), RST_BOSS_Z - 1, zf - t["membrane"]))
    tile_band = rrect(t["xr"] + 0.05, t["x1"] + t["slit"] - 0.1, t["y0"] - t["slit"] + 0.1, t["y1"] + t["slit"] - 0.1,
                      t["r"] + t["slit"] - 0.1) + \
        CS.square((t["x0"] - t["xr"] - 0.05, t["y1"] - t["y0"] + 2 * t["slit"] - 0.2)).translate((t["xr"] + 0.05, t["y0"] - t["slit"] + 0.1))
    # (edges sit inside the slit, never on its faces; square corners at the hinge root so the band is full width there)
    cuts.append(slab(tile_band, RST_BOSS_Z - 1, zf - FLEX_T))
    # charge LED light-pipe hole (vertical in print pose), through the tube and out into the recess front wall
    z_neck = H - RST["depth"] - FLEX_T            # 19.6: 1.85 slip hole below, 1.70 press-fit neck through the skin above
    cuts.append(M.cylinder(z_neck - lp["z0"] + 1, lp["hole_d"] / 2, circular_segments=32).translate((lp["x"], lp["y"], lp["z0"] - 1)))
    cuts.append(M.cylinder(H + 1 - z_neck + E, lp["neck_d"] / 2, circular_segments=32).translate((lp["x"], lp["y"], z_neck - E)))
    # 45 deg notch in the recess front wall over the pipe top, opening toward the rear, so the pipe end is seen whole
    hw = lp["hole_d"] / 2 + 0.6
    rise = H + 0.5 - (H - RST["depth"])
    cuts.append(M.batch_hull([box(lp["x"] - hw, lp["x"] + hw, lp["y"] - lp["hole_d"] / 2 - 0.3, lp["y"] + lp["hole_d"] / 2, H - RST["depth"] - E, H - RST["depth"]),
                              box(lp["x"] - hw, lp["x"] + hw, lp["y"] - lp["hole_d"] / 2 - 0.3 - rise, lp["y"] + lp["hole_d"] / 2 - rise, H + 0.5 - E, H + 0.5)]))
    # ---- antenna bore + 45 deg side-entry mouth
    # cuts use the path points of the other parity to the rib's, and stop 1.25 short of its closed end,
    # so no cut surface ever lies in the same plane as a rib face
    par = (gi[0] + 1) % 2
    last = gi[::2][-1] - 5
    bi = [i for i in range(len(PATH_S)) if PATH_S[i] >= GROOVE_S0 - 1.0 and i % 2 == par and i <= last]
    cuts.append(sweep_convex(bore_prof, bi))
    gc = [i for i in gi if i <= last]
    for run in np.split(np.array(gc), np.nonzero(np.diff([int(STRAIGHT[i]) for i in gc]))[0] + 1):
        if STRAIGHT[run[0]] and len(run) > 4:
            idx = [i for i in run if i % 2 == par][1:-1]      # stop one ring short of the corner tunnels (no shared faces)
            cuts.append(sweep_convex(band_prof, idx))
            cuts.append(sweep_convex(notch_prof, idx))
    # (no cut at the coin slots: the cover is left whole there, its seam face is what the coin levers on)
    cover = cover - union(cuts)
    # ---- nubs (after thinning)
    cover = cover + M.cylinder(H - FLEX_T - SW_TOP - NUB_GAP + E, b["nub_d"] / 2).translate((PRG_XY[0], PRG_XY[1], SW_TOP + NUB_GAP))
    cover = cover + M.cylinder(zf - FLEX_T - SW_TOP - NUB_GAP + E, t["nub_d"] / 2).translate((RST_XY[0], RST_XY[1], SW_TOP + NUB_GAP))
    return cover


# ===================================================================== shoe foot (black)
def leaf_cavity():
    """slot round the leaf: 0.6 slits both sides, 0.6 gap over it, open underneath (0.2 relief). All inside the neck."""
    lf = LEAF
    hw = lf["w"] / 2 + lf["slit"]
    ztop = Z_SHOE + lf["relief"] + lf["t"] + lf["gap"]
    return box(FOOT_X - hw, FOOT_X + hw, PL_Y1 - lf["len"], PL_Y1 + 1, Z_SHOE - 1, ztop)


def leaf_solid():
    lf = LEAF
    lz0 = Z_SHOE + lf["relief"]
    return box(FOOT_X - lf["w"] / 2, FOOT_X + lf["w"] / 2, PL_Y1 - lf["len"] - E, PL_Y1, lz0, lz0 + lf["t"])


def build_foot():
    xo = FAIR_IN + FAIR_T
    web = box(FOOT_X - xo, FOOT_X + xo, FL_Y0, FL_Y1, Z_WEB0, 0.0)
    walls = []
    for s in (-1, 1):
        a, b = FOOT_X + s * FAIR_IN, FOOT_X + s * xo
        prof = [(a, FAIR_Z), (b - s * 0.5, FAIR_Z), (b, FAIR_Z + 0.5), (b, Z_WEB0 + E), (a, Z_WEB0 + E)]
        walls.append(prism_y(prof, FL_Y0, FL_Y1))
    neck = box(FOOT_X - NECK_W / 2, FOOT_X + NECK_W / 2, PL_Y0, PL_Y1, Z_PL1 - E, Z_WEB0 + E)
    pw = PLATE_W / 2
    plate = prism_y([(FOOT_X - pw, Z_SHOE + 0.4), (FOOT_X - pw + 0.4, Z_SHOE), (FOOT_X + pw - 0.4, Z_SHOE),
                     (FOOT_X + pw, Z_SHOE + 0.4), (FOOT_X + pw, Z_PL1), (FOOT_X - pw, Z_PL1)], PL_Y0, PL_Y1)
    hz = HEEL["z"]
    heel = prism_x([(HEEL["y0"], hz), (HEEL["y1"], hz), (HEEL["y1"] + (Z_WEB0 - hz), Z_WEB0 + E), (HEEL["y0"], Z_WEB0 + E)],
                   FOOT_X - xo, FOOT_X + xo)
    foot = union([web, neck, plate, heel, key_shape()] + walls)
    foot = foot - prism_x([(PL_Y1 - 0.8, Z_SHOE - 1), (PL_Y1 + 1, Z_SHOE - 1), (PL_Y1 + 1, Z_SHOE + 0.8)], -30, 30)
    lf = LEAF
    cav, leaf, lz0 = leaf_cavity(), leaf_solid(), Z_SHOE + lf["relief"]
    ny = PL_Y1 - EST.lockpin_back
    nd = lf["relief"] + lf["nub"]
    nub = prism_x([(ny - 0.2 - nd, lz0 + E), (ny + 0.2 + nd, lz0 + E), (ny + 0.2, lz0 - nd), (ny - 0.2, lz0 - nd)],
                  FOOT_X - 1.5, FOOT_X + 1.5)
    # 45 deg root fillets on both leaf faces where the cavity ends (prints as a 45 deg chamfer, no support)
    yr = PL_Y1 - lf["len"]
    rf = LEAF_RF
    lt = lz0 + lf["t"]
    fil_top = prism_x([(yr - E, lt - E), (yr + rf, lt - E), (yr - E, lt + rf)], FOOT_X - lf["w"] / 2, FOOT_X + lf["w"] / 2)
    fil_bot = prism_x([(yr - E, lz0 + E), (yr + rf, lz0 + E), (yr - E, Z_SHOE)], FOOT_X - lf["w"] / 2, FOOT_X + lf["w"] / 2)
    foot = foot - cav + leaf + nub + fil_top + fil_bot
    return foot - pilot_voids()


def foot_ears():
    """print-only mouse ears on the foot's front (bed) face; see EAR."""
    e = EAR
    return union([M.cylinder(e["t"], e["r"], e["r"], 96).rotate((-90, 0, 0)).translate((FOOT_X + sx * e["dx"], PL_Y1 - e["t"], e["z"]))
                  for sx in (-1, 1)])


def pilot_voids():
    td = (CS.circle(0.8, 32) + CS.square((1.131, 1.131), True).rotate(45).translate((0, -0.4))).hull()
    return union([M.extrude(td, PILOT_D + E).translate((sx, sy, -PILOT_D)) for (sx, sy) in SCREWS])


def felt_parts():
    hz = HEEL["z"]
    heel_felt = box(FOOT_X - FAIR_IN, FOOT_X + FAIR_IN, HEEL["y0"] + 0.2, HEEL["y1"] - 0.2, Z_PAINT + 0.2, hz)
    rails = union([box(FOOT_X - 10.3, FOOT_X - 6.8, PL_Y0 + 1, PL_Y1 - 1, Z_WEB0 - FELT_T, Z_WEB0),
                   box(FOOT_X + 6.8, FOOT_X + 10.3, PL_Y0 + 1, PL_Y1 - 1, Z_WEB0 - FELT_T, Z_WEB0)])
    return heel_felt, rails


def screws_model():
    return union([M.cylinder(0.8, 2.0).translate((sx, sy, FLOOR - 1.0)) + M.cylinder(4.0, 1.0).translate((sx, sy, FLOOR - 1.0 - 4.0 + E))
                  for (sx, sy) in SCREWS])


# ===================================================================== camera proxy (X-E5, approximate)
CAM_SHOE_X = -4.0
Hc = 77.8


def camera_proxy():
    Wc = 129.5

    def rblock(x0, x1, y0, y1, z0, z1, r, rv=1.5):
        zs = [z0, z0 + rv * 0.3, z0 + rv, z1 - rv, z1 - rv * 0.3, z1]
        ins = [rv * 0.6, rv * 0.15, 0, 0, rv * 0.15, rv * 0.6]
        return M.batch_hull([slab(rrect(x0 + d, x1 - d, y0 + d, y1 - d, max(r - d, 0.2)), z - E, z) for z, d in zip(zs, ins)])
    rear = -CAM_REAR_Y
    body_all = rblock(-Wc / 2, Wc / 2, rear, rear + 31.0, 0, Hc, 5.0, 2.0)
    lower = body_all ^ box(-80, 80, -40, 40, -1, 63)
    upper = body_all ^ box(-80, 80, -40, 40, 63, 90)
    grip = rblock(36, 62, rear + 29, rear + 39.1, 8, 60, 4.0, 3.0)
    lens = M.cylinder(28, 30.5).rotate((-90, 0, 0)).translate((CAM_SHOE_X, rear + 35, 34.0))
    evf = rblock(-63, -41, rear - 7, rear + 1, 60, 75, 3.0, 1.5)
    eyecup = rblock(-62, -42, rear - 10, rear - 6.5, 61, 74, 3.0, 1.0)
    lcd = rblock(-50, 46, rear - 3, rear + 0.5, 8, 58, 2.0, 0.8)
    ssx = CAM_SHOE_X + EST.ss_dial_edge + 11.0
    fsx = CAM_SHOE_X - EST.fs_dial_edge - 8.5
    dials = union([M.cylinder(EST.fs_dial_h, 8.5).translate((fsx, -4, Hc)),
                   M.cylinder(EST.ss_dial_h, 11.0).translate((ssx, -3, Hc)),
                   M.cylinder(8.5, 9.5).translate((ssx + 23, -5, Hc))])
    sx = CAM_SHOE_X
    fl = EST.shoe_floor
    shoe = union([box(sx - 10.5, sx + 10.5, -9.5, 9.5, Hc, Hc + fl),
                  box(sx - 10.5, sx - 9.5, -9.0, 9.0, Hc + fl, Hc + fl + RAIL_H), box(sx + 9.5, sx + 10.5, -9.0, 9.0, Hc + fl, Hc + fl + RAIL_H),
                  box(sx - 10.5, sx - 6.3, -9.0, 9.0, Hc + fl + RAIL_H - 0.6, Hc + fl + RAIL_H),
                  box(sx + 6.3, sx + 10.5, -9.0, 9.0, Hc + fl + RAIL_H - 0.6, Hc + fl + RAIL_H)])
    lp_y = PLATE_L / 2 - EST.lockpin_back
    shoe = shoe - box(sx - 1.8, sx + 1.8, lp_y - 1.0, lp_y + 1.0, Hc - 1, Hc + fl + 0.01)   # lock-pin hole [EST]
    return dict(lower=lower, upper=upper, grip=grip, lens=lens, evf=evf, eyecup=eyecup, lcd=lcd, dials=dials, shoe=shoe)


def cam_place():
    """device -> camera frame translation (camera frame: shoe centre at x CAM_SHOE_X, y 0; paint at z Hc)."""
    return (CAM_SHOE_X - FOOT_X, -SHOE_Y, Hc - Z_PAINT)


# ===================================================================== print poses
def pose_matrix(name, m):
    """4x4 assembled -> print pose (min corner at the origin)."""
    if name == "cover":
        R = np.array([[-1, 0, 0, 0], [0, 1, 0, 0], [0, 0, -1, 0], [0, 0, 0, 1]], float)    # 180 about y: top face down
    elif name == "foot":
        R = np.array([[1, 0, 0, 0], [0, 0, 1, 0], [0, -1, 0, 0], [0, 0, 0, 1]], float)     # front end down (z' = -y)
    else:
        R = np.eye(4)
    mr = m.transform(R[:3])
    b = mr.bounding_box()
    T = np.eye(4); T[:3, 3] = (-b[0], -b[1], -b[2])
    return T @ R


def apply(m, T):
    return m.transform(np.asarray(T)[:3])


# ===================================================================== checks
def solid_vol(m):
    if m.is_empty():
        return 0.0
    v = 0.0
    for comp in m.decompose():
        bb = comp.bounding_box()
        if min(bb[3] - bb[0], bb[4] - bb[1], bb[5] - bb[2]) > 0.02:
            v += comp.volume()
    return v


def raster(cs, x0, y0, nx, ny, res):
    xs = x0 + (np.arange(nx) + 0.5) * res
    ys = y0 + (np.arange(ny) + 0.5) * res
    X, Y = np.meshgrid(xs, ys)
    pts = np.column_stack([X.ravel(), Y.ravel()])
    inside = np.zeros(len(pts), bool)
    for c in cs.to_polygons():
        c = np.asarray(c)
        if len(c) < 3:
            continue
        inside ^= MPath(c).contains_points(pts)
    return inside.reshape(ny, nx)


def runs_1d(row):
    d = np.diff(np.concatenate([[0], row.astype(np.int8), [0]]))
    return list(zip(np.nonzero(d == 1)[0], np.nonzero(d == -1)[0]))


def bridge_test(C, B, max_span=10.0, res=0.05):
    """B = material in this layer that is already supported (by the layer below or a 45 deg step)."""
    """Is region C (unsupported in this layer) a bridge? Lines across C in some direction must land on material of
    the layer below at BOTH ends, and be <= max_span. Returns (kind, angle, span)."""
    # a small hole inside a bridge (the RST light hole) is spanned by the bridge lines like in any slicer
    def sarea(q):
        q = np.asarray(q); return 0.5 * float(np.sum(q[:, 0] * np.roll(q[:, 1], -1) - np.roll(q[:, 0], -1) * q[:, 1]))
    polys = [q for q in C.to_polygons() if sarea(q) > 0 or abs(sarea(q)) >= 3.0]
    if len(polys) != len(C.to_polygons()):
        C = CS([[tuple(map(float, v)) for v in q] for q in polys])
    x0, y0, x1, y1 = C.bounds()
    clip = CS.square((x1 - x0 + 3, y1 - y0 + 3)).translate((x0 - 1.5, y0 - 1.5))
    Bc = B ^ clip
    best_one = 0.0
    for ang in range(0, 180, 15):
        Cr, Br = C.rotate(ang), Bc.rotate(ang)
        a0, b0, a1, b1 = Cr.bounds()
        a0, b0, a1, b1 = a0 - 0.6, b0 - 0.6, a1 + 0.6, b1 + 0.6
        nx, ny = int((a1 - a0) / res) + 1, int((b1 - b0) / res) + 1
        c = raster(Cr, a0, b0, nx, ny, res)
        if c.sum() == 0:
            return "sliver", ang, 0.0
        b = raster(Br, a0, b0, nx, ny, res)
        k = int(0.45 / res) + 1
        tot = good = one = 0
        span = 0.0
        for j in range(ny):
            for i0, i1 in runs_1d(c[j]):
                n = i1 - i0
                tot += n
                la = b[j, max(0, i0 - k):i0].any()
                ra = b[j, i1:i1 + k].any()
                if la and ra and n * res <= max_span:
                    good += n
                    span = max(span, n * res)
                elif la or ra:
                    one += n
        if tot and good / tot >= 0.9:
            return "bridge", ang, span
        best_one = max(best_one, one / max(tot, 1))
    return ("one-sided ledge" if best_one > 0.5 else "island"), -1, 0.0


def layer_scan(m, max_span=10.0, tol=0.03):
    """Per-layer support analysis of a part in print pose: each layer may grow 0.2 (45 deg) past the one below
    (+tol); anything beyond must be a bridge anchored both ends. Returns (bridges, failures)."""
    b = m.bounding_box()
    zs = np.arange(LAYER / 2, b[5], LAYER)
    prev = None
    bridges, fails = [], []
    for z in zs:
        S = m.slice(float(z))
        if prev is not None and not S.is_empty():
            U = S - prev.offset(LAYER + tol, JoinType.Round)
            U = U.offset(-0.05, JoinType.Round).offset(0.05, JoinType.Round)
            for C in U.decompose():
                if C.area() < 0.03:
                    continue
                kind, ang, span = bridge_test(C, S - U, max_span)
                bb = C.bounds()
                rec = (round(z - LAYER / 2, 2), kind, span, C.area(), bb, C, float(z))
                if kind == "bridge":
                    bridges.append(rec)
                elif kind != "sliver":
                    fails.append(rec)
        prev = S
    return bridges, fails


def steep_faces(m, max_deg=45.0, tol=1.0):
    t = tm(m)
    n = t.face_normals
    cz = t.triangles_center[:, 2]
    area = t.area_faces
    down = (n[:, 2] < 0) & (cz > 0.02)
    ang = np.degrees(np.arcsin(np.clip(-n[:, 2], 0, 1)))
    steep = down & (ang > max_deg + tol) & (ang < 89.0)
    worst = float(ang[down & (ang < 89.0)].max()) if np.any(down & (ang < 89.0)) else 0.0
    return worst, float(area[steep].sum())


def thin_scan(m, T_to_frame, step, w, zones, axis_name):
    """Slices of the part (in a frame where the slice normal is z) at 'step'; regions thinner than w are found by a
    morphological opening. Each region is mapped back to the assembled frame and matched against named zones.
    Returns list of (zone or None, point, area)."""
    mf_ = apply(m, T_to_frame)
    Ti = np.linalg.inv(T_to_frame)
    b = mf_.bounding_box()
    out = []
    for z in np.arange(b[2] + step / 2, b[5], step):
        S = mf_.slice(float(z))
        if S.is_empty():
            continue
        op = S.offset(-w / 2, JoinType.Round).offset(w / 2, JoinType.Round)
        thin = (S - op).offset(-0.03, JoinType.Round).offset(0.03, JoinType.Round)
        for C in thin.decompose():
            if C.area() < 0.06:
                continue
            x0, y0, x1, y1 = C.bounds()
            if max(x1 - x0, y1 - y0) < 0.45:      # corner residue of an opening, not a wall
                continue
            p = Ti @ np.array([(x0 + x1) / 2, (y0 + y1) / 2, z, 1.0])
            out.append((in_zone(p, zones), tuple(np.round(p[:3], 2)), round(C.area(), 3), axis_name))
    return out


def in_zone(p, zones):
    for (nm, bx_) in zones:
        if callable(bx_):
            if bx_(p):
                return nm
        elif bx_[0] <= p[0] <= bx_[1] and bx_[2] <= p[1] <= bx_[3] and bx_[4] <= p[2] <= bx_[5]:
            return nm
    return None


def near_mouth(p):
    """True if p is on the snap-in mouth lips of a straight antenna run (the 45 deg lips taper by design)."""
    d = np.linalg.norm(PATH_XY - p[:2], axis=1)
    k = int(np.argmin(d))
    if d[k] > 3.0 or not STRAIGHT[k] or PATH_S[k] < GROOVE_S0:
        return False
    l = float((p[:2] - PATH_XY[k]) @ PATH_N[k])
    return -0.6 <= l <= BORE_R + 1.1 and RIB_FLOOR - 0.05 <= p[2] <= ANT_Z + BORE_R + 0.05


def near_entry(p):
    """True within 1.6 mm of the groove's entry face, where the rising bore meets the face (an edge, not a wall)."""
    d = np.linalg.norm(PATH_XY - p[:2], axis=1)
    k = int(np.argmin(d))
    return d[k] < 2.6 and GROOVE_S0 - 0.3 <= PATH_S[k] <= GROOVE_S0 + 1.6 and p[2] >= RIB_FLOOR - 0.5


def ray_thin(m, T_to_frame, w, zones, step=0.25):
    """Columns normal to the print layers: any material run shorter than w is a thin skin. Returns like thin_scan."""
    mf_ = apply(m, T_to_frame)
    Ti = np.linalg.inv(T_to_frame)
    b = mf_.bounding_box()
    out = []
    for x in np.arange(b[0] + step / 2 + 0.013, b[3], step):
        for y in np.arange(b[1] + step / 2 + 0.017, b[4], step):
            hs = mf_.ray_cast((x, y, b[5] + 1), (x, y, b[2] - 1))
            zz = [h.position[2] for h in hs]
            if len(zz) % 2:
                continue
            for k in range(0, len(zz), 2):
                th = zz[k] - zz[k + 1]
                if th < w:
                    p = Ti @ np.array([x, y, (zz[k] + zz[k + 1]) / 2, 1.0])
                    out.append((in_zone(p, zones), tuple(np.round(p[:3], 2)), round(th, 3), "layer-normal ray"))
    return out


def frames():
    """slice frames: z (horizontal layers), x and y (vertical sections)."""
    Fz = np.eye(4)
    Fx = np.array([[0, 1, 0, 0], [0, 0, 1, 0], [1, 0, 0, 0], [0, 0, 0, 1]], float)    # x becomes the slice normal
    Fy = np.array([[0, 0, 1, 0], [1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 0, 1]], float)    # y becomes the slice normal
    return dict(z=Fz, x=Fx, y=Fy)


def first_hit(m, p0, p1):
    h = m.ray_cast(tuple(map(float, p0)), tuple(map(float, p1)))
    return np.array(h[0].position) if h else None


def x_runs(m, y, z, x0=-25.0, x1=25.0):
    s = m ^ box(x0, x1, y - 0.01, y + 0.01, z - 0.01, z + 0.01)
    if s.is_empty():
        return []
    return sorted((c.bounding_box()[0], c.bounding_box()[3]) for c in s.decompose())


def min_radius(xyz, k=4):
    r = []
    for i in range(k, len(xyz) - k):
        a, b, c = xyz[i - k], xyz[i], xyz[i + k]
        ab, bc, ca = np.linalg.norm(b - a), np.linalg.norm(c - b), np.linalg.norm(a - c)
        area2 = np.linalg.norm(np.cross(b - a, c - a))
        if area2 > 1e-9:
            r.append(ab * bc * ca / (2 * area2))
    return min(r) if r else float("inf")


# ===================================================================== 3MF (Bambu Studio opens it as a plate)
# X1C bed exclusion area (the "X1 Carbon 0.4 nozzle" machine profile, as test_coupons reads it): x 0..18, y 0..28
BED_EXCLUDE = (0.0, 0.0, 18.0, 28.0)
EXCLUDE_MARGIN = 3.0
# one plate per colour, so no filament swaps: black (base + foot) and silver (cover). Each 3MF is one Bambu plate on
# filament 1; load the plate's colour and print it. Per-object settings go in Metadata/model_settings.config.
FOOT_OBJ = dict(brim_type="no_brim")            # the mouse ears hold it; a brim would run into the open leaf slits
PLATES = {"black": [("base (black PETG)", "base", (110.0, 128.0), {}), ("foot (black PETG, mouse ears, no brim)", "foot", (168.0, 128.0), FOOT_OBJ)],
          "silver": [("cover (silver PETG)", "cover", (128.0, 128.0), {})]}


def write_3mf(path, items, plate=256.0):
    """items: list of (name, trimesh in print pose (min corner at origin), (cx, cy) plate centre for the part, settings).
    Writes the model plus Metadata/model_settings.config (per-object name, extruder 1 and any extra settings)."""
    import xml.sax.saxutils as sx
    objs, builds = [], []
    cfg = ['<?xml version="1.0" encoding="UTF-8"?>', "<config>"]
    for i, (name, t, (cx, cy), oset) in enumerate(items, start=1):
        cfg.append(f'  <object id="{i}">')
        cfg.append(f'    <metadata key="name" value="{sx.escape(name)}"/>')
        cfg.append('    <metadata key="extruder" value="1"/>')
        for kk, vv in oset.items():
            cfg.append(f'    <metadata key="{kk}" value="{sx.escape(str(vv))}"/>')
        cfg += [f'    <part id="{i}" subtype="normal_part">', f'      <metadata key="name" value="{sx.escape(name)}"/>', '    </part>', '  </object>']
        v = np.asarray(t.vertices); f = np.asarray(t.faces)
        vs = "".join(f'<vertex x="{a:.4f}" y="{b:.4f}" z="{c:.4f}"/>' for a, b, c in v)
        fs = "".join(f'<triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in f)
        objs.append(f'<object id="{i}" name="{name}" type="model"><mesh><vertices>{vs}</vertices><triangles>{fs}</triangles></mesh></object>')
        ex = t.extents
        tx, ty = cx - ex[0] / 2, cy - ex[1] / 2
        builds.append(f'<item objectid="{i}" transform="1 0 0 0 1 0 0 0 1 {tx:.4f} {ty:.4f} 0" printable="1"/>')
    model = ('<?xml version="1.0" encoding="UTF-8"?>\n'
             '<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">'
             '<metadata name="Application">xe5-remote camera_block build.py</metadata>'
             '<metadata name="Title">XE5 remote camera block (PETG, 0.4 nozzle, 0.2 layers, supports OFF)</metadata>'
             f'<resources>{"".join(objs)}</resources><build>{"".join(builds)}</build></model>')
    ct = ('<?xml version="1.0" encoding="UTF-8"?>\n<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
          '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
          '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
    rels = ('<?xml version="1.0" encoding="UTF-8"?>\n<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", ct)
        z.writestr("_rels/.rels", rels)
        z.writestr("3D/3dmodel.model", model)
        z.writestr("Metadata/model_settings.config", "\n".join(cfg + ["</config>"]) + "\n")
    # bed fit: 5 mm margin, EXCLUDE_MARGIN clear of the bed exclusion area, 3 mm between parts, 10 mm round a standing foot
    fits, rects = [], []
    for (name, t, (cx, cy), _) in items:
        ex = t.extents
        r = (cx - ex[0] / 2, cy - ex[1] / 2, cx + ex[0] / 2, cy + ex[1] / 2)
        rects.append((name, r))
        ex0, ey0, ex1, ey1 = BED_EXCLUDE
        clear_ex = r[0] >= ex1 + EXCLUDE_MARGIN or r[1] >= ey1 + EXCLUDE_MARGIN or r[2] <= ex0 - EXCLUDE_MARGIN or r[3] <= ey0 - EXCLUDE_MARGIN
        fits.append((name, r[0] >= 5 and r[2] <= plate - 5 and r[1] >= 5 and r[3] <= plate - 5 and clear_ex))
    for i in range(len(rects)):
        for j in range(i + 1, len(rects)):
            (na, a), (nb, b) = rects[i], rects[j]
            g = 10.0 if ("foot" in na or "foot" in nb) else 3.0
            if a[0] < b[2] + g and b[0] < a[2] + g and a[1] < b[3] + g and b[1] < a[3] + g:
                fits.append((f"{na} too close to {nb} (< {g:.0f})", False))
    return fits


# ===================================================================== MEASURE-knob sweep (self-test)
def probe():
    """--probe: build base + foot for the current knob values (env CB_FOOT_X / CB_REAR_OVERHANG) and print a JSON line."""
    import json
    base, foot = build_base(), build_foot()
    bodies = {nm: len([q for q in m.decompose() if abs(q.volume()) > 0.01]) for nm, m in (("base", base), ("foot", foot))}
    pil = union([M.cylinder(PILOT_D, 0.8, circular_segments=32).translate((sx, sy, -PILOT_D)) for (sx, sy) in SCREWS])
    rim = union([M.cylinder(PILOT_D - 0.2, 1.8, circular_segments=48).translate((sx, sy, -PILOT_D + 0.1)) for (sx, sy) in SCREWS])
    key_on_foot = solid_vol(key_shape() - box(-50, 50, FL_Y0, FL_Y1, -10, 10))
    print("PROBE " + json.dumps(dict(foot_x=D.FOOT_X, ro=D.REAR_OVERHANG, fl_y0=FL_Y0, heel_ok=FL_Y0 >= PROUD - 1e-9, bodies=bodies,
                                     g_scr=pil.min_gap(leaf_cavity(), 5.0), rim_void=solid_vol((rim - foot) - pilot_voids()),
                                     key_off_foot=key_on_foot, overlap=solid_vol(base ^ foot),
                                     dial_ok=min(EST.ss_dial_edge - (W2 - FOOT_X), EST.fs_dial_edge - (W2 + FOOT_X)) >= D.DIAL_MIN_GAP)))


SWEEP_RO = (-2.0, -1.0, 0.0, 1.0, 2.0, 3.0, 4.0, 5.0)


def selftest_sweep():
    """re-run this file in --probe mode across REAR_OVERHANG -2..+5 and FOOT_X over its dial-legal range."""
    import json, subprocess
    from concurrent.futures import ThreadPoolExecutor
    fx_lo = math.ceil((W2 + D.DIAL_MIN_GAP - EST.ss_dial_edge) * 10 - 1e-6) / 10       # 1.1
    fx_hi = math.floor((EST.fs_dial_edge - D.DIAL_MIN_GAP - W2) * 10 + 1e-6) / 10      # 5.4
    combos = [(D.FOOT_X, ro) for ro in SWEEP_RO] + [(fx, ro) for fx in (fx_lo, fx_hi) for ro in (-1.0, 0.0, 5.0)]

    def run(fr):
        env = dict(os.environ, CB_FOOT_X=str(fr[0]), CB_REAR_OVERHANG=str(fr[1]))
        r = subprocess.run([sys.executable, os.path.abspath(__file__), "--probe"], env=env, capture_output=True, text=True, timeout=300)
        line = [l for l in r.stdout.splitlines() if l.startswith("PROBE ")]
        return json.loads(line[0][6:]) if line else dict(foot_x=fr[0], ro=fr[1], error=r.stderr[-300:])
    with ThreadPoolExecutor(max_workers=6) as ex:
        res = list(ex.map(run, combos))
    bad, legal = [], []
    for r in res:
        tag = f"FOOT_X {r['foot_x']} RO {r['ro']}"
        if "error" in r:
            bad.append(f"{tag}: crashed {r['error']}")
            continue
        if r["bodies"] != {"base": 1, "foot": 1}:
            bad.append(f"{tag}: bodies {r['bodies']}")
        if r["key_off_foot"] > 0.001 or r["overlap"] > 0.01 or r["g_scr"] < 1.0 or r["rim_void"] > 0.01 or not r["dial_ok"]:
            bad.append(f"{tag}: key off foot {r['key_off_foot']:.3f}, base x foot {r['overlap']:.3f}, screw to leaf {r['g_scr']:.2f}, "
                       f"pilot wall short {r['rim_void']:.3f}, dial ok {r['dial_ok']}")
        if r["heel_ok"] != (r["fl_y0"] >= PROUD - 1e-9):
            bad.append(f"{tag}: heel guard wrong")
        if r["heel_ok"]:
            legal.append(r["ro"])
    ro_min = PROUD - (CAM_REAR_Y + PLATE_L / 2 - 22.6) + 0.0
    return bad, res, ro_min, fx_lo, fx_hi


# ===================================================================== main
def main():
    if "--probe" in sys.argv:
        probe()
        return
    out = HERE
    fails = []
    report = []

    def ok(cond, msg):
        report.append(("PASS " if cond else "FAIL ") + msg)
        if not cond:
            fails.append(msg)

    base, cover, foot = build_base(), build_cover(), build_foot()
    dropped = {}
    for nm in ("base", "cover", "foot"):
        m = dict(base=base, cover=cover, foot=foot)[nm]
        comps = sorted(m.decompose(), key=lambda q: -q.volume())
        dropped[nm] = (len(comps) - 1, sum(abs(q.volume()) for q in comps[1:]))
        m = comps[0]
        ms = m.simplify(1e-4)                      # collapse sub-micron facet slivers left by the booleans
        if abs(ms.volume() - m.volume()) < 0.01 and len(ms.decompose()) == 1:
            m = ms
        if nm == "base": base = m
        elif nm == "cover": cover = m
        else: foot = m
    parts = dict(base=base, cover=cover, foot=foot)
    # what goes on the plate: the foot carries its print-only mouse ears there (never in the assembly)
    foot_pr = foot + foot_ears()
    printable = dict(base=base, cover=cover, foot=foot_pr)
    c = contents()
    felt_heel, felt_rails = felt_parts()
    scr = screws_model()

    # ---------------------------------------------------------- dims
    asm = union([base, cover, foot])
    bb = asm.bounding_box()
    report.append(f"DIMS body {L:.1f} L x {2 * W2:.1f} W x {H:.1f} H (black body {L - 2 * PROUD:.1f} x {2 * W2 - 2 * PROUD:.1f}); "
                  f"with foot {bb[4] - bb[1]:.1f} x {bb[3] - bb[0]:.1f} x {bb[5] - bb[2]:.1f}; top face {H - Z_PAINT:.1f} above the paint, "
                  f"body underside {-Z_PAINT:.1f}, fairing {FAIR_Z - Z_PAINT:.1f}")
    report.append(f"     seam z {SPLIT:.1f} (silver cap {H - SPLIT:.1f} over black {SPLIT:.1f}); cell keep-out {CELL_L} x {CELL_W} x {CELL_H} "
                  f"(pocket {POCKET_W} x {STOP_Y0 - Y_IN0:.2f} to the cell stop); lead bay {CELL_Y0 - Y_IN0:.1f}; "
                  f"antenna service-loop bay in the nose y {LOOP_YA - 1.6:.1f}..{Y_IN1 - 0.1:.1f} (body {L - (Y_CELL_FRONT + WALL):.1f} longer than the cell alone needs)")

    # ---------------------------------------------------------- watertight single body
    import io
    for nm, m in list(parts.items()) + [("foot as printed (with mouse ears)", foot_pr)]:
        t = tm(m)
        nb = len(m.decompose())
        rt = trimesh.load(io.BytesIO(trimesh.exchange.stl.export_stl(t)), file_type="stl")   # as a slicer reads the STL
        ok(t.is_watertight and rt.is_watertight and rt.is_volume and nb == 1 and m.status() == mf.Error.NoError,
           f"{nm}: watertight {t.is_watertight}, after STL round trip (vertices merged by position) watertight {rt.is_watertight} "
           f"and a closed volume {rt.is_volume}, bodies {nb}, genus {m.genus()}, {m.volume():.0f} mm3")

    ok(all(v <= 0.01 for (n, v) in dropped.values()),
       "boolean shards dropped (any detached piece over 0.01 mm3 is a real body, so it fails): " +
       ", ".join(f"{k} {n} ({v:.4f} mm3)" for k, (n, v) in dropped.items()))
    # foot-to-base interfaces follow the shoe; the foot never shows behind the body
    kb = key_shape().bounding_box()
    ok(FL_Y0 >= PROUD - 1e-9 and kb[1] >= FL_Y0 - 1e-6 and kb[4] <= FL_Y1 + 1e-6 and all(FL_Y0 + 1.5 <= sy <= FL_Y1 - 1.5 for (_, sy) in SCREWS),
       f"foot on the body: foot y {FL_Y0:.2f}..{FL_Y1:.2f} starts >= {PROUD} (the black body's rear face, so it never shows behind it); "
       f"keys y {kb[1]:.2f}..{kb[4]:.2f} and screws y {', '.join(f'{sy:.1f}' for _, sy in SCREWS)} all on the foot "
       f"(REAR_OVERHANG {D.REAR_OVERHANG}, SHOE_Y {SHOE_Y:.1f})")
    # ---------------------------------------------------------- keep-outs and part overlaps
    worst = []
    for nm, m in parts.items():
        for cn, cm in c.items():
            if cn == "screen" or (nm == "cover" and cn == "light_pipe"):
                continue                     # light pipe vs cover: press fit, checked on its own below
            v = solid_vol(m ^ cm)
            if v > 0.01:
                worst.append(f"{nm} x {cn} {v:.3f} mm3")
    ok(not worst, "contents / keep-out intersections = 0 (" + ", ".join(sorted(c.keys())) + ")" + ("" if not worst else ": " + "; ".join(worst)))
    # battery plug: free run from the SH1.25 socket mouths back to the first plastic, measured on the built parts, vs the
    # mated plug + wire bend keep-out; and the board-underside contents must not overlap each other
    runs = []
    for xx in np.linspace(bx(EST.sock_v[0]) + 0.1, bx(EST.sock_v[1]) - 0.1, 7):
        for zz in np.linspace(PCB_Z0 - EST.sock_drop + 0.1, PCB_Z0 - 0.1, 4):
            hh = [first_hit(mm, (float(xx), by(0.0) - 0.001, float(zz)), (float(xx), -5.0, float(zz))) for mm in (base, cover)]
            runs.append(min(by(0.0) - h_[1] for h_ in hh if h_ is not None))
    cc = [(a_, b_, solid_vol(c[a_] ^ c[b_])) for a_, b_ in (("sockets", "lead_bay"), ("sockets", "sh_plug"), ("sh_plug", "pcb"),
                                                            ("sh_wire_bend", "pcb"), ("sh_plug", "usb_overmold"), ("sh_plug", "battery"),
                                                            ("sh_wire_bend", "battery"), ("lead_bay", "battery"))]
    need_pl = EST.plug_proud + EST.plug_bend
    ok(min(runs) >= need_pl and all(v < 0.01 for *_, v in cc),
       f"battery plug: free run from the SH1.25 socket mouths (y {by(0.0):.1f}) back to the first plastic {min(runs):.2f} at 28 points "
       f"(>= mated plug {EST.plug_proud} [EST] + wire exit and bend {EST.plug_bend} [EST] = {need_pl:.1f}); board USB edge {-USB_WALL_U:.1f} in from "
       f"the pocket rear face; content overlaps " + ", ".join(f"{a_} x {b_} {v:.3f}" for a_, b_, v in cc))
    po = []
    for a, b2 in (("base", "cover"), ("base", "foot"), ("cover", "foot")):
        v = solid_vol(parts[a] ^ parts[b2])
        if v > 0.01:
            po.append(f"{a} x {b2} {v:.3f}")
    ok(not po, "part-to-part overlaps assembled = 0" + ("" if not po else ": " + "; ".join(po)))
    v = solid_vol(foot ^ scr)
    report.append(f"INFO M2 self-tapper thread engagement in the foot pilots: {v:.2f} mm3 (intended)")

    # ---------------------------------------------------------- MEASURE-knob sweep
    if "--no-sweep" not in sys.argv:
        bad, res, ro_min, fx_lo, fx_hi = selftest_sweep()
        trip = [r["ro"] for r in res if "error" not in r and not r["heel_ok"]]
        ok(not bad, f"self-test sweep ({len(res)} rebuilds): REAR_OVERHANG {SWEEP_RO[0]}..{SWEEP_RO[-1]} and FOOT_X {fx_lo}..{fx_hi} (dial-legal): "
           f"one body per part every time, keys and screws stay on the foot, no base x foot overlap, screw to leaf cavity >= 1.0, pilot wall >= 1.0"
           + ("" if not bad else ": " + "; ".join(bad[:6])))
        report.append(f"     REAR_OVERHANG legal range >= {ro_min:.1f} (below that the foot would show behind the body; the 'foot on the body' "
                      f"check fails, as it should, for RO {', '.join(str(t) for t in sorted(set(trip))) or 'none'} in the sweep)")

    # ---------------------------------------------------------- USB
    ovm = c["usb_overmold"]
    ok(solid_vol(ovm ^ asm) < 0.01, f"USB: 12 x 6.5 r1 overmold seats against the receptacle face (y {REC_FACE_Y:.1f}) with no contact; "
       f"notch {PORT['w']:.1f} wide, top z {PORT['top']:.2f} (overmold {OVM_Z[0]:.2f}..{OVM_Z[1]:.2f}, floor = base wall top {SPLIT:.1f})")
    side = (PORT["w"] - EST.usb_env[0]) / 2
    report.append(f"     clearances: sides {side:.2f}, top {PORT['top'] - OVM_Z[1]:.2f}, bottom {OVM_Z[0] - SPLIT:.2f}; roof over the notch {H - PORT['top']:.2f}")
    cam = camera_proxy()
    pl = cam_place()
    cable = union([ovm, M.cylinder(40, EST.usb_cable_d / 2).rotate((90, 0, 0)).translate((0, REC_FACE_Y - EST.usb_env[2], USB_ZC))]).translate(pl)
    camu = union(list(cam.values()))
    ok(solid_vol(cable ^ camu) < 0.01, "USB reachable on camera: overmold + 40 mm of cable straight back clears the camera proxy "
       f"(EVF/eyecup gap {cable.min_gap(union([cam['evf'], cam['eyecup']]), 50):.1f} mm)")

    # ---------------------------------------------------------- dial clearance (parameters)
    right = W2 - FOOT_X
    left = W2 + FOOT_X
    gss, gfs = EST.ss_dial_edge - right, EST.fs_dial_edge - left
    ok(min(gss, gfs) >= D.DIAL_MIN_GAP, f"dials [EST]: body edge to shutter dial {gss:.2f}, to film-sim dial {gfs:.2f} (>= {D.DIAL_MIN_GAP}); "
       f"FOOT_X {FOOT_X}, body half-width {W2:.2f}")
    devu = union([m.translate(pl) for m in (base, cover, foot)])
    ok(solid_vol(devu ^ camu) < 0.01, "unit on the camera proxy: no contact with dials, EVF, top plate (only the foot in the shoe)")

    # ---------------------------------------------------------- snaps
    ys = SNAP_YS[0]
    cw = [r for r in x_runs(cover, ys, SPLIT + 3.0, 14.0, 25.0)]
    wall_face = max(r[1] for r in cw if r[1] < CAV_X)
    br = x_runs(base, ys, (BUMP_Z[1] + BUMP_Z[2]) / 2, 14.0, 25.0)
    bump_tip = min(r[0] for r in br if r[1] > CAV_X)
    interf = wall_face - bump_tip
    arm_L = BUMP_Z[1] - SPLIT - ROOT_F / 2
    strain = 1.5 * ARM_T * interf / arm_L ** 2 * 100
    skins = []
    for z in (SPLIT + 1.0, BUMP_Z[1], ARM_TOP - ARM_T, ARM_TOP - 0.3, ARM_TOP + 0.3):
        runs = [r for r in x_runs(cover, ys, z, 15.0, 25.0) if r[1] > 18.5]
        skins.append(runs[-1][1] - runs[-1][0] if runs else 0.0)
    ok(0.3 - 1e-3 <= interf <= 0.45 + 1e-3 and strain <= 1.5 and min(skins) >= 1.0,
       f"snaps x4: measured interference {interf:.2f} (0.30-0.45), 45 deg lead-in + 45 deg retain face, root fillet {ROOT_F}, "
       f"arm {ARM_T} x {ARM_W} free length {arm_L:.2f}, strain {strain:.2f}%, relief {ARM_RELIEF}, outer skin min {min(skins):.2f}")

    # ---------------------------------------------------------- antenna
    xyz = np.column_stack([PATH_XY, PATH_Z])
    sel = PATH_S <= EST.ant_len
    ant_len_route = float(np.sum(np.linalg.norm(np.diff(xyz, axis=0), axis=1)))
    rmin = min_radius(xyz[sel], k=4)
    gap_seam = float(PATH_Z[sel].min() - EST.ant_d / 2 - SPLIT)
    ant = c["antenna"]
    cell = c["battery"]
    dcell = ant.min_gap(cell, 20.0)
    hit = solid_vol(ant ^ asm)
    # barrier: straight lines from points round the cable to points on the cell top must cross >= 1 mm of plastic or board
    solid_bar = union([base, cover])
    pcb = c["pcb"]
    worst_bar = 99.0; worst_pl = 99.0; wp = None
    gx = np.linspace(-CELL_W / 2 + 1, CELL_W / 2 - 1, 7); gy = np.linspace(CELL_Y0 + 1, CELL_Y1 - 1, 9)
    targets = [(x, y, BATT_Z1) for x in gx for y in gy]
    all_pts = [i for i in range(len(PATH_S)) if PATH_S[i] <= EST.ant_len][::4]
    worst_fan = 99.0
    for i in all_pts:
        p = np.array([PATH_XY[i][0], PATH_XY[i][1], PATH_Z[i]])
        near = (float(np.clip(p[0], -CELL_W / 2, CELL_W / 2)), float(np.clip(p[1], CELL_Y0, CELL_Y1)), BATT_Z1)
        fan = sorted(targets, key=lambda q: np.linalg.norm(np.array(q) - p))[:8] if PATH_S[i] >= GROOVE_S0 else []
        for kq, tq in enumerate([near] + fan):
            q = np.array(tq)
            d = q - p; dist = np.linalg.norm(d)
            a = p + d / dist * (EST.ant_d / 2)

            def seg_len(mm):
                hs = mm.ray_cast(tuple(a), tuple(q))
                pos = [np.linalg.norm(np.array(h.position) - a) for h in hs]
                ins = 0.0
                if len(pos) % 2 == 1:
                    pos = [0.0] + pos
                for k in range(0, len(pos) - 1, 2):
                    ins += pos[k + 1] - pos[k]
                return ins
            plast = seg_len(solid_bar)
            fr4 = seg_len(pcb)
            if kq == 0:
                if plast + fr4 < worst_bar:
                    worst_bar, wp = plast + fr4, (tuple(np.round(p, 1)), round(plast, 2), round(fr4, 2))
                if PATH_S[i] >= GROOVE_S0:
                    worst_pl = min(worst_pl, plast)
            else:
                worst_fan = min(worst_fan, plast + fr4)
    # plastic straight below the cable on the side lanes (directly over the cell)
    below = []
    for i in [i for i in range(len(PATH_S)) if GROOVE_S0 <= PATH_S[i] <= EST.ant_len and deep_lane(PATH_XY[i])][::8]:
        for l in (-0.8, 0.0, 0.8):
            px, py = PATH_XY[i] + l * PATH_N[i]
            zc = PATH_Z[i] - math.sqrt(max((EST.ant_d / 2) ** 2 - l * l, 0))
            hs = cover.ray_cast((px, py, zc - 0.001), (px, py, BATT_Z1))
            pos = [h.position[2] for h in hs]
            th = sum(pos[k] - pos[k + 1] for k in range(0, len(pos) - 1, 2))
            below.append(th)
    ok(ant_len_route >= EST.ant_len - 0.5 and rmin >= EST.ant_bend_r and gap_seam >= 2.0 and hit < 0.01 and worst_bar >= 1.0 and min(below) >= 1.0,
       f"antenna: route {ant_len_route:.1f} mm for a {EST.ant_len:.0f} mm whip (groove {ANT_TOTAL - GROOVE_S0:.0f} mm, ends {GROOVE_EXTRA:.0f} past the tip), "
       f"min bend r {rmin:.2f} (>= {EST.ant_bend_r}), {gap_seam:.1f} above the seam, contact with the shell {hit:.3f} mm3; "
       f"gap to the swollen cell {dcell:.1f}; plastic straight under the cable on the cell lanes min {min(below):.2f}; "
       f"shortest line cable->cell crosses >= {worst_bar:.2f} mm of plastic or board along the whole whip "
       f"(plastic alone {worst_pl:.2f} along the groove; board alone over the plug; worst at {wp})")
    report.append(f"     slanted lines from the groove to the 8 nearest cell points: >= {worst_fan:.2f} mm of plastic or board")
    # service length: free cable from the IPEX centre to the groove entrance vs the distance when the cover stands on its
    # front seam edge (rotated about y = L, z = SPLIT), 0..180 deg
    i0 = int(np.argmax(PATH_S >= GROOVE_S0))
    free_len = PLUG_OFF + float(np.sum(np.linalg.norm(np.diff(xyz[:i0 + 1], axis=0), axis=1)))
    P_ipex = np.array([IPEX_XY[0], IPEX_XY[1], PLUG_Z])
    ent = xyz[i0]

    def d_open(deg):
        t = math.radians(deg)
        dy, dz = ent[1] - L, ent[2] - SPLIT
        q = np.array([ent[0], L + dy * math.cos(t) + dz * math.sin(t), SPLIT - dy * math.sin(t) + dz * math.cos(t)])
        return float(np.linalg.norm(q - P_ipex))
    dmax90 = max(d_open(a) for a in range(0, 91, 5))
    ok(free_len >= dmax90 + FREE_MARGIN,
       f"antenna service length: free cable IPEX -> groove entrance {free_len:.1f} >= {dmax90:.1f} (cover open 90 deg on its front edge) "
       f"+ {FREE_MARGIN:.0f}; distance closed {d_open(0):.1f}, 30 deg {d_open(30):.1f}, 90 deg {d_open(90):.1f}, 180 deg {d_open(180):.1f}")
    free_sol = antenna_solid(0.0, GROOVE_S0 - 3.0)
    g_close = free_sol.min_gap(cover, 5.0)
    g_base = free_sol.min_gap(base, 5.0)
    loop_r = min_radius(xyz[:i0], k=4)
    ok(g_close >= 1.5 and loop_r >= EST.ant_bend_r and g_base >= 0.3,
       f"antenna free run (plug to 3 mm before the groove): gap to every cover feature that closes down on it (posts, front face, "
       f"top skin, groove rib) {g_close:.2f} (>= 1.5); loop bend r {loop_r:.2f} (>= {EST.ant_bend_r}); resting gap to the base deck {g_base:.2f}")
    pry_c = (PRY_Y[0] + PRY_Y[1]) / 2
    snap_clear = SNAP_YS[0] + ARM_W / 2 + 0.6 + ROOT_F      # end of the y 15 snap's tongue gap (19.1)
    ok(pry_c <= 0.4 * L and PRY_Y[0] >= snap_clear + 0.5 and PRY_Y[1] <= SNAP_YS[1] - ARM_W / 2 - 1.1 - 0.5,
       f"coin slots in both side walls at y {PRY_Y[0]}..{PRY_Y[1]}: centre {pry_c:.1f} in the rear 40% of {L:.1f} (<= {0.4 * L:.1f}), "
       f"{PRY_Y[0] - snap_clear:.1f} clear of the y {SNAP_YS[0]:.0f} snap gap, {IPEX_XY[1] - PRY_Y[1]:.1f} behind the IPEX: "
       f"opening lifts the rear and the cover hinges on its front edge")
    # coin test (test_coupons C1): a 22.5 x 1.85 coin held flat in the seam plane, centred in the slot, pushed in square
    # to the side wall until its rim is COIN_REACH past the base skin. It must touch neither part. Then bisect how far it
    # really goes. Also measure the slot itself on the real base (mouth, depth from the cover face, wall behind it).
    Rc, tc = COIN[0] / 2, COIN[1]
    zc = SPLIT - PRY[1] / 2
    skin = W2 - PROUD                               # base skin (the cover face is at W2)
    for sx in (-1, 1):
        def coin_at(depth):                         # depth = how far the rim is past the base skin
            return M.cylinder(tc, Rc, Rc, 256).translate((sx * (skin - depth + Rc), pry_c, zc - tc / 2))
        c0 = coin_at(COIN_REACH)
        vb, vc = vol(base ^ c0), vol(cover ^ c0)
        gap = min(base.min_gap(c0, 1.0), cover.min_gap(c0, 1.0))
        lo_, hi_ = 0.0, 3.0
        for _ in range(30):
            mid = (lo_ + hi_) / 2
            cm = coin_at(mid)
            if vol(base ^ cm) + vol(cover ^ cm) > 1e-4:
                hi_ = mid
            else:
                lo_ = mid
        # slot as built: base material along a ray in -sx from outside, at the slot centre, several heights
        dep = []
        for zz in np.linspace(SPLIT - PRY[1] + 0.1, SPLIT - 0.1, 5):
            h = first_hit(base, (sx * (W2 + 2), pry_c, zz), (0.0, pry_c, zz))
            dep.append(W2 - abs(h[0]) if h is not None else 0.0)
        wall_b = (W2 - min(dep)) - CAV_X
        # mouth along y and z, measured: open at 1.0 past the base skin over the full 12.0 x 2.2
        probe_open = vol(base ^ box(min(sx * (skin - 1.0), sx * (W2 + 1)), max(sx * (skin - 1.0), sx * (W2 + 1)),
                                    PRY_Y[0] + 0.02, PRY_Y[1] - 0.02, SPLIT - PRY[1] + 0.02, SPLIT + E))
        ok(vb < 1e-4 and vc < 1e-4 and gap >= 0.05 and min(dep) >= 1.2 and probe_open < 1e-4 and wall_b >= 1.0,
           f"coin slot {'right' if sx > 0 else 'left'}: {PRY[0]:.1f} wide x {PRY[1]} tall in the base under the seam, measured "
           f"{min(dep):.2f} deep from the cover face (>= 1.2), open over the full mouth to 1.0 past the base skin ({probe_open:.4f} mm3 in it), "
           f"wall behind {wall_b:.2f} (>= 1.0), cover whole; coin {COIN[0]} x {COIN[1]} flat in the seam plane reaches {COIN_REACH} past the base "
           f"skin touching nothing (overlap base {vb:.4f} / cover {vc:.4f}, min gap {gap:.2f}); it stops {lo_:.2f} past the base skin")
    report.append(f"     retained: teardrop bore {2 * BORE_R:.1f} for the {EST.ant_d} sleeve; on the straight runs a 45 deg side-entry mouth {MOUTH} "
                  f"(snaps past {EST.ant_d - MOUTH:.1f}); the four r{ANT_R:.0f} corners are closed tunnels (thread the tip through)")

    # ---------------------------------------------------------- buttons
    for nm, (x, y), xy_top in (("PRG", PRG_XY, (PRG["cx"] + 1.8, PRG["cy"])), ("RST", RST_XY, ((RST["x0"] + RST["x1"]) / 2, (RST["y0"] + RST["y1"]) / 2))):
        h = first_hit(cover, (x, y, SW_TOP + 0.001), (x, y, H + 2))
        gap = h[2] - SW_TOP
        top = first_hit(cover, (xy_top[0], xy_top[1], H + 2), (xy_top[0], xy_top[1], 0))
        rec = H - top[2]
        need = gap + EST.btn_travel
        if nm == "PRG":
            lever = PRG["cy"] + PRG["r"] + PRG["band"] / 2 - y
            Lh = PRG["band"] + PRG["slit"]
            fp = slab(prg_thin_cs(), H - FLEX_T - need - 0.3, H - FLEX_T - 0.001)
            cols = [(PRG["cx"], float(yy)) for yy in np.arange(PRG["cy"] + PRG["r"] - 0.3, PRG["cy"] + PRG["r"] + PRG["band"] - 0.05, 0.1)]
        else:
            lever = x - (RST["x0"] - RST["band"] / 2)          # hinge on the outboard (-x) edge
            Lh = RST["band"]
            fp = slab(rrect(RST["xr"], RST["x1"], RST["y0"], RST["y1"], RST["r"]), H - RST["depth"] - FLEX_T - need - 0.3, H - RST["depth"] - FLEX_T - 0.001)
            cols = [(float(xx), (RST["y0"] + RST["y1"]) / 2) for xx in np.arange(RST["xr"] + 0.05, RST["x0"] - 0.05, 0.1)]
        # measured skin along the hinge axis (columns every 0.1): a notch on the tension face would show here
        sk_t = []
        for (hx, yy) in cols:
            hs = cover.ray_cast((hx + 0.013, float(yy), H + 1), (hx + 0.013, float(yy), H - 3))
            if len(hs) >= 2:
                sk_t.append(hs[0].position[2] - hs[1].position[2])
        t_min, t_max = min(sk_t), max(sk_t)
        strain = t_max * need / (2 * Lh * lever) * 100
        nubm = M.cylinder(5, 1.2).translate((x, y, SW_TOP))
        free = solid_vol((fp - nubm) ^ union(list(c.values())))
        lim = 2.0 if nm == "PRG" else 2.5
        ok(abs(gap - NUB_GAP) < 0.02 and free < 0.01 and strain <= lim and t_min >= FLEX_T - 0.03 and t_max <= FLEX_T + 0.03,
           f"{nm}: nub gap {gap:.2f} + switch travel {EST.btn_travel} = {need:.2f} press; nothing else under the {FLEX_T} flexure "
           f"within {need + 0.3:.2f} ({free:.3f} mm3); top {'recessed' if rec > 0.05 else 'flush'} {rec:.2f}; hinge skin measured along "
           f"the hinge axis {t_min:.2f}..{t_max:.2f} over {len(sk_t)} columns (uniform {FLEX_T}, no notch); hinge strain ~{strain:.2f}% (<= {lim})")
    report.append(f"     RST tile {RST['x1'] - RST['x0']:.1f} x {RST['y1'] - RST['y0']:.1f} square-ish in a {RST['depth']} recess, hinged on its outboard (-x) edge (band {RST['band']}); "
                  f"PRG disc d{2 * PRG['r']:.1f} flush with orange dimple. Sacrificial skin under the RST slit: {RST['membrane']} (cut once)")

    # ---------------------------------------------------------- charge LED light pipe
    lp = LIGHT_PIPE
    led_cs = rrect(bx(EST.led_v[0]), bx(EST.led_v[1]), by(EST.led_u[0]), by(EST.led_u[1]), 0.01)
    over_led = (CS.circle(EST.pipe_d / 2, 48).translate((lp["x"], lp["y"])) ^ led_cs).area()
    led_top = bz(EST.led_h)
    pz = H - RST["depth"]                       # pipe cut flush with the recess floor
    blocked, nrays = 0, 0
    for th in (60, 70, 80):
        for az in (-20, 0, 20):
            t, a_ = math.radians(th), math.radians(az)
            d = np.array([math.sin(t) * math.sin(a_), -math.sin(t) * math.cos(a_), math.cos(t)])
            for (ox, oy) in ((0, 0), (0.6, 0), (-0.6, 0), (0, 0.6), (0, -0.6)):
                p0 = np.array([lp["x"] + ox, lp["y"] + oy, pz + 0.01])
                nrays += 1
                if cover.ray_cast(tuple(p0 + d * 0.02), tuple(p0 + d * 60)) or base.ray_cast(tuple(p0 + d * 0.02), tuple(p0 + d * 60)):
                    blocked += 1
    hole = M.cylinder(H + 1 - lp["z0"], lp["hole_d"] / 2, circular_segments=32).translate((lp["x"], lp["y"], lp["z0"]))
    bore = bore_solid()
    g_bore = hole.min_gap(bore, 5.0)
    g_hinge = hole.min_gap(slab(rrect(RST["xr"], RST["x1"] + RST["slit"], RST["y0"] - RST["slit"], RST["y1"] + RST["slit"], 0.5),
                                RST_BOSS_Z - 1, H - RST["depth"]), 5.0)
    ok(over_led >= 0.5 and lp["z0"] - led_top <= 1.0 and blocked == 0 and g_bore >= 1.0 and lp["tube_r"] - lp["hole_d"] / 2 >= 0.8
       and g_hinge >= 1.0,
       f"charge LED: clear {EST.pipe_d} filament light pipe in a {lp['hole_d']:.2f} hole, its foot {lp['z0'] - led_top:.2f} over the LED "
       f"({over_led:.2f} mm2 over the LED top), top flush with the RST recess floor; {nrays - blocked}/{nrays} rays from its top face at 60/70/80 deg "
       f"from vertical toward the rear (+-20 deg) leave the unit unobstructed; hole to antenna bore {g_bore:.2f} (>= 1.0), to the RST hinge "
       f"and slits {g_hinge:.2f} (>= 1.0); tube wall {lp['tube_r'] - lp['hole_d'] / 2:.2f}")
    # pipe held in z: 1.70 press-fit neck through the skin; it overlaps the cover ONLY in the neck; moved 0.2 either way
    # it still overlaps (no free slide), and the tube's bottom end is at the pipe foot (the assembly reference face)
    pipe = c["light_pipe"]
    z_neck = H - RST["depth"] - FLEX_T
    ov_all = solid_vol(pipe ^ cover)
    ov_out = solid_vol((pipe - box(-30, 30, -10, 90, z_neck - 0.01, H + 1)) ^ cover)
    ov_dn, ov_up = solid_vol(pipe.translate((0, 0, -0.2)) ^ cover), solid_vol(pipe.translate((0, 0, 0.2)) ^ cover)
    ends = [first_hit(cover, (lp["x"] + dx, lp["y"] + dy, 5.0), (lp["x"] + dx, lp["y"] + dy, H + 2)) for dx, dy in ((1.35, 0), (-1.35, 0), (0, 1.35), (0, -1.35))]
    tube_end = min(e[2] for e in ends if e is not None)
    interf = EST.pipe_d - lp["neck_d"]
    ok(0.03 <= interf <= 0.1 + 1e-9 and ov_all > 0.02 and ov_out < 0.001 and min(ov_dn, ov_up) > 0.02 and abs(tube_end - lp["z0"]) < 0.02,
       f"light pipe retained: {lp['neck_d']:.2f} press-fit neck (interference {interf:.2f} on dia) through the {FLEX_T} skin, "
       f"{lp['hole_d']:.2f} slip hole below; overlap with the cover {ov_all:.3f} mm3, all of it in the neck ({ov_out:.4f} outside); "
       f"moved 0.2 down / up it still grips ({ov_dn:.3f} / {ov_up:.3f} mm3); tube bottom end at z {tube_end:.2f} = pipe foot {lp['z0']:.2f} "
       f"(push the pipe flush with it, trim the top flush with the recess floor, drop of CA)")
    # RST hinge measured on the real cover (pipe hole and every cut included): full skin width along the root line,
    # thin zone ends AT the root (no thin skin running on past it), solid boss behind the root
    t = RST
    tw = t["y1"] - t["y0"]
    zf = H - t["depth"]

    def strip_w(x0, x1, z):
        S = cover.slice(float(z)) ^ CS.square((x1 - x0, tw)).translate((x0, t["y0"]))
        return S.area() / (x1 - x0)
    root_skin = min(strip_w(xa, xa + 0.2, zf - FLEX_T / 2) for xa in np.arange(t["xr"] + 0.05, t["x0"] - 0.2, 0.2))
    under_band = max(strip_w(xa, xa + 0.2, zf - FLEX_T - 0.3) for xa in np.arange(t["xr"] + 0.05, t["x0"] - 0.2, 0.2))
    behind_root = strip_w(t["xr"] - 0.4, t["xr"] - 0.05, zf - FLEX_T - 0.3)
    ok(root_skin >= tw - 0.05 and under_band < 0.05 and behind_root >= tw - 0.05,
       f"RST hinge (outboard edge, root x {t['xr']:.1f}): skin width along the band and root line {root_skin:.2f} (tile {tw:.1f}, "
       f"pipe hole and all cuts included); under the band {under_band:.2f} wide (open, flexes); boss behind the root {behind_root:.2f} wide "
       f"(thin zone ends at the root)")

    # ---------------------------------------------------------- screen window
    brk_cs = rrect(bx(EST.bracket_v[0]), bx(EST.bracket_v[1]), by(EST.bracket_u[0]), by(EST.bracket_u[1]), EST.bracket_r)
    hide_glass = (cover.slice(WIN_Z + 0.05) ^ brk_cs.offset(0.25, JoinType.Round)).area()
    hide_top = (cover.slice(H - 0.05) ^ brk_cs.offset(0.25, JoinType.Round)).area()
    sight_hits = 0
    gy0 = by(EST.glass_u[0])
    for xg in np.linspace(bx(EST.glass_v[0]), bx(EST.glass_v[1]), 7):
        p0 = np.array([xg, gy0, BRK_TOP - 0.02])
        d = np.array([0.0, -math.cos(math.radians(30)), math.sin(math.radians(30))])
        if cover.ray_cast(tuple(p0 + d * 0.01), tuple(p0 + d * 40)):
            sight_hits += 1
    g_win = bore.min_gap(window_cut(), 5.0)
    g_pocket = bore.min_gap(slab(rrect(bx(EST.bracket_v[0]) - BRK_LAT, bx(EST.bracket_v[1]) + BRK_LAT, by(EST.bracket_u[0]) - BRK_LAT,
                                       by(EST.bracket_u[1]) + BRK_LAT, EST.bracket_r + BRK_LAT), PCB_Z1, BRK_TOP + BRK_CLR), 5.0)
    wb = window_cs().bounds()
    ok(hide_glass < 0.01 and hide_top < 0.01 and sight_hits == 0 and min(g_win, g_pocket) >= 1.0,
       f"screen window {wb[2] - wb[0]:.1f} x {wb[3] - wb[1]:.1f} r{EST.bracket_r + 0.3:.1f} = OLED bracket + 0.3 per side, 45 deg frame: cover over the "
       f"bracket at glass level {hide_glass:.3f} mm2, at the top face {hide_top:.3f} mm2; 30 deg rear sight line from the glass [EST] edge "
       f"clear ({7 - sight_hits}/7); wall antenna bore to the window {g_win:.2f}, to the bracket pocket {g_pocket:.2f} (>= 1.0)")

    # ---------------------------------------------------------- print poses, layer grid, overhangs
    poses = {nm: pose_matrix(nm, m) for nm, m in printable.items()}
    pm = {nm: apply(m, poses[nm]) for nm, m in printable.items()}
    func = dict(base=[("floor", FLOOR), ("seam / board rest", SPLIT), ("tongue top", SPLIT + TONGUE_H), ("cell pocket top", BATT_Z1)] +
                [(f"bump z{i}", z) for i, z in enumerate(BUMP_Z)] + [("arm top", ARM_TOP), ("fence top", PCB_Z0 + 1.2)],
                cover=[("seam", H - SPLIT), ("groove top", H - SPLIT - TONGUE_H - 0.2), ("top skin", H - TOP_IN),
                       ("RST recess", RST["depth"]), ("RST membrane", RST["depth"] + RST["membrane"]), ("RST tile", RST["depth"] + FLEX_T),
                       ("PRG disc", FLEX_T), ("nub tips", H - SW_TOP - NUB_GAP), ("RST boss", H - RST_BOSS_Z), ("light-pipe neck", RST["depth"] + FLEX_T), ("USB notch roof", H - PORT["top"]),
                       ("retain face", H - BUMP_Z[0]), ("recess top", H - BUMP_Z[3] - 0.2)],
                foot=[("foot height", FL_Y1 - FL_Y0), ("leaf root", LEAF["len"])])
    offgrid = [f"{p}:{n}={v:.2f}" for p, l in func.items() for n, v in l if abs(round(v / LAYER) * LAYER - v) > 0.011]
    ok(not offgrid, "functional heights on whole 0.2 layers in print pose" + ("" if not offgrid else ": " + ", ".join(offgrid)))
    ph = {}
    all_bridges = {}
    for nm in ("base", "cover", "foot"):
        m = pm[nm]
        b = m.bounding_box()
        ph[nm] = (b[3] - b[0], b[4] - b[1], b[5] - b[2])
        worst_a, steep = steep_faces(m)
        bridges, bad = layer_scan(m)
        all_bridges[nm] = bridges
        wb = max([r[2] for r in bridges], default=0.0)
        ok(not bad and steep < 0.5, f"overhang {nm} (print pose {ph[nm][0]:.1f} x {ph[nm][1]:.1f} x {ph[nm][2]:.1f}): "
           f"{len(bridges)} bridges (longest {wb:.1f} <= 10, all anchored both ends), {len(bad)} one-sided ledges/islands; "
           f"faces steeper than 46 deg {steep:.2f} mm2 (worst facet {worst_a:.1f})")
        for r in bad[:12]:
            report.append(f"       {r[1]} at layer z {r[0]:.1f} area {r[3]:.2f} bbox {np.round(r[4], 1).tolist()}")
        for r in sorted(bridges, key=lambda q: -q[2])[:4]:
            report.append(f"       bridge z {r[0]:.1f} span {r[2]:.1f} area {r[3]:.1f} bbox {np.round(r[4], 1).tolist()}")
    report.append("INFO modelled break-away support: none needed (all three parts print support-free with slicer supports OFF)")

    # ---------------------------------------------------------- shoe foot leaf spring
    lf = LEAF
    leaf_pm = apply(leaf_solid(), poses["foot"])
    over_leaf = []
    for r in all_bridges["foot"]:
        C, zc = r[5], r[6]
        if r[2] <= 2.0:
            continue
        for dz in np.arange(0.1, 1.01, 0.1):
            Sl = leaf_pm.slice(float(zc - dz))
            if not Sl.is_empty() and (C ^ Sl).area() > 0.01:
                over_leaf.append(f"bridge z {r[0]:.1f} span {r[2]:.1f} is {dz:.1f} over the leaf")
                break
    fpm = pm["foot"]
    fb = fpm.bounding_box()
    narrow = []
    for z in np.arange(LAYER / 2, fb[5], LAYER):
        S = fpm.slice(float(z))
        if S.is_empty():
            continue
        G = S.offset(0.2, JoinType.Round).offset(-0.2, JoinType.Round) - S
        for C in G.decompose():
            x0, y0, x1, y1 = C.bounds()
            pc = np.linalg.inv(poses["foot"]) @ np.array([(x0 + x1) / 2, (y0 + y1) / 2, z, 1.0])
            if any(math.hypot(pc[0] - sx, pc[1] - sy) < 2.0 for (sx, sy) in SCREWS):
                continue                     # 45 deg roof of a teardrop pilot, not a gap
            if PL_Y1 - LEAF["len"] - 0.05 <= pc[1] <= PL_Y1 - LEAF["len"] + LEAF_RF + 0.05 and abs(pc[0] - FOOT_X) <= LEAF["w"] / 2 + LEAF["slit"]:
                continue                     # the leaf gap closing into its 45 deg root fillet (tapers to nothing by design)
            if C.area() >= 0.02 and max(x1 - x0, y1 - y0) >= 0.5:
                narrow.append(f"z {z:.1f} {np.round(C.bounds(), 1).tolist()}")
    wing = []
    for dx in (-8.5, -7.5, -6.6, 6.6, 7.5, 8.5):
        for yy in np.arange(PL_Y1 - 6.0, PL_Y1 - 0.9, 0.5):
            hs = foot.ray_cast((FOOT_X + dx, float(yy), Z_SHOE - 1), (FOOT_X + dx, float(yy), Z_WEB0 - 0.05))
            if len(hs) >= 2:
                wing.append(hs[1].position[2] - hs[0].position[2])
    leaf_strain = 1.5 * lf["t"] * lf["nub"] / LEAF_FREE ** 2 * 100
    in_neck = NECK_W / 2 - (lf["w"] / 2 + lf["slit"])
    ax = poses["foot"][:3, :3] @ np.array([0.0, 1.0, 0.0])          # leaf long axis in print pose
    vertical = abs(ax[2]) > 0.7
    s_lim = LEAF_STRAIN_LIM if vertical else 1.5
    yr_ = PL_Y1 - lf["len"]
    lz0_ = Z_SHOE + lf["relief"]
    f_top = solid_vol(foot ^ box(FOOT_X - lf["w"] / 2 + 0.1, FOOT_X + lf["w"] / 2 - 0.1, yr_ + 0.02, yr_ + LEAF_RF, lz0_ + lf["t"] + 0.02, lz0_ + lf["t"] + LEAF_RF))
    f_bot = solid_vol(foot ^ box(FOOT_X - lf["w"] / 2 + 0.1, FOOT_X + lf["w"] / 2 - 0.1, yr_ + 0.02, yr_ + LEAF_RF, Z_SHOE, lz0_ - 0.02))
    w_in = lf["w"] - 0.2
    f_top_need = 0.5 * (LEAF_RF - 0.02) ** 2 * w_in * 0.8
    f_bot_need = 0.5 * (LEAF_RF - 0.02) * (lf["relief"] - 0.02) * w_in * 0.5
    pil = union([M.cylinder(PILOT_D, 0.8, circular_segments=32).translate((sx, sy, -PILOT_D)) for (sx, sy) in SCREWS])
    g_scr = pil.min_gap(leaf_cavity(), 5.0)
    rim = union([M.cylinder(PILOT_D - 0.2, 1.8, circular_segments=48).translate((sx, sy, -PILOT_D + 0.1)) for (sx, sy) in SCREWS])
    rim_void = solid_vol((rim - foot) - pilot_voids())
    ok(not over_leaf and not narrow and min(wing) >= PLATE_T - 0.05 and leaf_strain <= s_lim and in_neck >= 1.0
       and f_top >= f_top_need and f_bot >= f_bot_need and g_scr >= 1.0 and rim_void < 0.01,
       f"foot leaf spring along y, {lf['w']} x {lf['t']} x {lf['len']} inside the neck ({in_neck:.1f} of neck each side of its slits): root to nub "
       f"{LEAF_FREE:.1f}, nub {lf['nub']}, strain {leaf_strain:.2f}% (<= {s_lim}: long axis {'VERTICAL in print, bends across the layers, derated' if vertical else 'in the layer plane'}); "
       f"45 deg root fillets measured {f_top:.2f} / {f_bot:.2f} mm3 (gap face / relief face, >= {f_top_need:.2f} / {f_bot_need:.2f}); "
       f"screw pilots to the leaf cavity {g_scr:.2f} (>= 1.0), pilot wall >= 1.0 all round ({rim_void:.3f} mm3 missing); plate wings full {min(wing):.2f} thick (>= {PLATE_T - 0.05:.2f}); "
       f"every printed gap in the print-pose layers >= 0.4 ({'none narrower' if not narrow else '; '.join(narrow[:4])}); "
       f"no bridge over 2 mm within 1.0 above the leaf ({'none' if not over_leaf else '; '.join(over_leaf[:3])})")

    # ---------------------------------------------------------- standing foot: bed contact (coupon C5 lesson)
    # faces of the print-pose foot lying on the bed (z' = 0, facing down), with and without the mouse ears; the coupon
    # rule is 90 mm2 at 18 tall, scaled here with the height (the foot is taller and top-heavy)
    def bed_faces(mm):
        tt = tm(mm)
        dn = (tt.face_normals[:, 2] < -0.999) & (tt.triangles[:, :, 2].max(1) < 1e-3)
        return float(tt.area_faces[dn].sum()), tt.triangles[dn].reshape(-1, 3)
    bed, bpts = bed_faces(pm["foot"])
    bed0, bpts0 = bed_faces(apply(foot, poses["foot"]))
    hf = pm["foot"].bounding_box()[5]
    bed_need = FOOT_MIN_BED_18 * hf / 18.0
    tip_d, tip_d0 = np.ptp(bpts[:, 1]), np.ptp(bpts0[:, 1])
    ears = foot_ears()
    g_leaf = ears.min_gap(leaf_cavity(), 5.0)
    g_plate = ears.min_gap(box(FOOT_X - PLATE_W / 2, FOOT_X + PLATE_W / 2, PL_Y1 - 1.0, PL_Y1, Z_SHOE, Z_PL1), 5.0)
    ear_body = len([q for q in (foot + ears).decompose() if abs(q.volume()) > 0.01])
    ok(bed >= bed_need and g_leaf >= 0.2 and g_plate >= 0.4 and ear_body == 1,
       f"standing foot: {hf:.1f} tall on {bed:.1f} mm2 of bed contact with 2 mouse ears r{EAR['r']} x {EAR['t']} (bare foot {bed0:.1f}); "
       f"needs >= {bed_need:.0f} (coupon rule 90 at 18 tall, scaled to the height); tipping depth {tip_d:.1f} (bare {tip_d0:.1f}); "
       f"ears {g_leaf:.2f} clear of the leaf slits (>= 0.2) and {g_plate:.2f} clear of the plate and its 0.8 lead-in (>= 0.4), one body with the foot; "
       f"print-only (the assembled foot has none)")

    # ---------------------------------------------------------- wall thickness
    zones = {
        "base": [("snap arm 0.8 (bevel edges)", (16.5, 21, -1, L + 1, SPLIT - 0.6, ARM_TOP + 0.1)), ("snap arm 0.8 (bevel edges)", (-21, -16.5, -1, L + 1, SPLIT - 0.6, ARM_TOP + 0.1)),
                 ("tongue 0.9", (-21, 21, -1, L + 1, SPLIT - 0.05, SPLIT + TONGUE_H + 0.1)),
                 ("board fences + rest ledges (1.0, edges)", (-14, 14, -1, L + 1, PCB_Z0 - 1.5, PCB_Z0 + 1.3)),
                 ("bottom roll edge", (-21, 21, -1, L + 1, -0.1, 0.25)),
                 ("key groove", (-12, 18, 1, 26, -0.1, 1.1))],
        "cover": [("snap slot lip", (16.5, 21, -1, L + 1, SPLIT - 0.1, H)), ("snap slot lip", (-21, -16.5, -1, L + 1, SPLIT - 0.1, H)),
                  ("antenna snap-in mouth lips", near_mouth),
                  ("antenna groove entry edge", near_entry),
                  ("PRG flexure 0.8", (PRG["cx"] - 5, PRG["cx"] + 5, PRG["cy"] - 5, PRG["cy"] + PRG["r"] + PRG["band"] + 1, H - 1.6, H + 0.1)),
                  ("RST flexure / recess", (RST_FLOOR[0] - 2.5, RST_FLOOR[1] + 2.5, RST_FLOOR[2] - 2.5, RST_FLOOR[3] + 2.5, RST_BOSS_Z - 0.1, H + 0.1)),
                  ("window frame 45", (-11.8, 11.8, by(EST.bracket_u[0]) - 1.7, by(EST.bracket_u[1]) + 1.7, TOP_IN - 0.1, H + 0.1)),
                  ("bay lead chamfer", (-BAY_X - 0.1, BAY_X + 0.1, Y_IN1 - 0.3, Y_IN1 + 1.2, SPLIT - 0.1, SPLIT + TONGUE_H + 1.4)),
                  ("USB surround / roll over the notch", (-8, 8, -0.1, 2.2, SPLIT - 0.1, H + 0.1)),
                  ("LED light-pipe tube", (LIGHT_PIPE["x"] - 2.3, LIGHT_PIPE["x"] + 2.3, LIGHT_PIPE["y"] - 2.3, LIGHT_PIPE["y"] + 2.3,
                                             LIGHT_PIPE["z0"] - 0.1, H - RST["depth"] - FLEX_T + 0.1)),
                  ],
        "foot": [("leaf spring 0.8", (FOOT_X - LEAF["w"] / 2 - LEAF["slit"] - 0.05, FOOT_X + LEAF["w"] / 2 + LEAF["slit"] + 0.05,
                                      PL_Y1 - LEAF["len"] - 0.1, PL_Y1 + 0.1, Z_SHOE - 1.0, Z_SHOE + LEAF["relief"] + LEAF["t"] + LEAF["gap"] + 0.05)),
                 ("leaf nub", (FOOT_X - 1.6, FOOT_X + 1.6, PL_Y1 - EST.lockpin_back - 1.2, PL_Y1 - EST.lockpin_back + 1.2, Z_SHOE - 1.0, Z_SHOE + 0.3)),
                 ("plate lead-in / fairing chamfer", (-30, 30, -1, 30, Z_SHOE - 1.5, Z_SHOE + 0.5)),
                 ("fairing tip chamfer", (-30, 30, -1, 30, FAIR_Z - 0.1, FAIR_Z + 0.6)),
                 ("key rib top", (-12, 18, 1, 26, 0.0, 1.1)),
                 ("screw pilot", (-10, 15, 5, 20, -3.2, 0.1)),
                 ] + [("mouse ear 0.4 (print-only, snapped off)", (min(FOOT_X + sx * (EAR["dx"] - EAR["r"] - 0.1), FOOT_X + sx * (EAR["dx"] + EAR["r"] + 0.1)),
                                                              max(FOOT_X + sx * (EAR["dx"] - EAR["r"] - 0.1), FOOT_X + sx * (EAR["dx"] + EAR["r"] + 0.1)),
                                                              PL_Y1 - EAR["t"] - 0.6, PL_Y1 + 0.1, EAR["z"] - EAR["r"] - 0.1, EAR["z"] + EAR["r"] + 0.1))
                      for sx in (-1, 1)]}
    fr = frames()
    thin_all = {}
    for nm, m in printable.items():
        res98, res78 = [], []
        res98 += thin_scan(m, poses[nm], LAYER, 0.98, zones[nm], "layer")
        res78 += thin_scan(m, poses[nm], LAYER, 0.78, zones[nm], "layer")
        rr = ray_thin(m, poses[nm], 0.98, zones[nm])
        res98 += rr
        res78 += [r for r in rr if r[2] < 0.78]
        bad98 = [r for r in res98 if r[0] is None]
        bad78 = [r for r in res78 if r[0] is None or not r[0][-3:].replace(".", "").isdigit()]
        bad78 = [r for r in res78 if r[0] is None]
        z98 = sorted(set(r[0] for r in res98 if r[0]))
        zmin = {}
        for r in res98:
            if r[0] and r[3] == "layer-normal ray":
                zmin[r[0]] = min(zmin.get(r[0], 9.0), r[2])
        z98 = [f"{z}" + (f" (min {zmin[z]:.2f} normal to layers)" if z in zmin else "") for z in z98]
        thin_all[nm] = (bad98, bad78)
        ok(not bad98 and not bad78, f"wall thickness {nm}: no wall under 1.0 outside the named thin features; nothing under 0.8 "
           f"outside them either. Under 1.0 only in: {', '.join(z98) or 'none'}")
        for r in (bad98 + bad78)[:10]:
            report.append(f"       thin at {r[1]} area {r[2]} ({r[3]}-slices)")
    # measured skins
    skin = dict(
        top_skin=H - first_hit(cover, (0, 12.0 + 3, 30), (0, 15, 0))[2] if False else None)
    pts = {"cover top skin (x 0, y 12)": ((0.0, 12.0, H + 1), (0.0, 12.0, 0)),
           "roof over the USB notch (x 0, y 2.5)": ((0.0, 2.5, H + 1), (0.0, 2.5, 0)),
           "base floor under the cell (x 0, y 30)": ((0.0, 30.0, -1), (0.0, 30.0, 30)),
           "RST tile (0.8 flexure)": (((RST["x0"] + RST["x1"]) / 2 + 1.5, RST["y0"] + 1.0, H + 1), ((RST["x0"] + RST["x1"]) / 2 + 1.5, RST["y0"] + 1.0, 0)),
           "PRG disc (0.8 flexure)": ((PRG["cx"] - 2.0, PRG["cy"], H + 1), (PRG["cx"] - 2.0, PRG["cy"], 0)),
           "RST sacrificial skin": ((RST["x0"] + 1.0, RST["y0"] - RST["slit"] / 2, H + 1), (RST["x0"] + 1.0, RST["y0"] - RST["slit"] / 2, 0))}
    sk = []
    for nm, (p0, p1) in pts.items():
        mm = base if "base" in nm else cover
        hs = mm.ray_cast(p0, p1)
        if len(hs) >= 2:
            sk.append(f"{nm} {abs(hs[1].position[2] - hs[0].position[2]):.2f}")
    report.append("     measured skins: " + "; ".join(sk))

    # ---------------------------------------------------------- exports (print pose)
    T = {nm: tm(m) for nm, m in pm.items()}
    for nm, t in T.items():
        t.export(os.path.join(out, f"{ID}_{nm}_print.stl"))
    tm(asm).export(os.path.join(out, f"{ID}_assembled.stl"))
    trimesh.util.concatenate([tm(v) for v in c.values()]).export(os.path.join(out, f"{ID}_contents.stl"))
    old = os.path.join(out, f"{ID}_plate.3mf")          # the old mixed-colour single plate is replaced by one plate per colour
    if os.path.exists(old):
        os.remove(old)
    import zipfile as _zf
    for pk_, items in PLATES.items():
        pth = os.path.join(out, f"{ID}_plate_{pk_}.3mf")
        fits = write_3mf(pth, [(nm, T[part], xy, oset) for (nm, part, xy, oset) in items])
        with _zf.ZipFile(pth) as z:
            names = z.namelist()
            ms = z.read("Metadata/model_settings.config").decode() if "Metadata/model_settings.config" in names else ""
        n_ext = ms.count('key="extruder" value="1"')
        foot_nb = ("foot" not in "".join(p[1] for p in items)) or 'key="brim_type" value="no_brim"' in ms
        ok(all(f for _, f in fits) and n_ext == len(items) and foot_nb,
           f"3MF {pk_} plate ({os.path.basename(pth)}): {', '.join(p[0] for p in items)}; one colour per plate, every object on filament 1 "
           f"in Metadata/model_settings.config ({n_ext}/{len(items)}){'; foot brim_type no_brim stored per object' if 'foot' in ''.join(p[1] for p in items) else ''}; "
           f"inside the 256 x 256 X1C bed (5 mm margin), >= {EXCLUDE_MARGIN:.0f} clear of the bed exclusion area {BED_EXCLUDE}, 10 mm round the standing foot"
           + ("" if all(f for _, f in fits) else ": " + "; ".join(n for n, f in fits if not f)))

    # ---------------------------------------------------------- print the report
    print("\n".join(report))
    print("")
    if fails:
        print(f"{len(fails)} CHECK(S) FAILED:")
        for f_ in fails:
            print("  - " + f_)
    else:
        print("ALL CHECKS PASSED")
    open(os.path.join(out, "checks.txt"), "w").write("\n".join(report) + "\n\n" + ("ALL CHECKS PASSED" if not fails else
                                                     "FAILED:\n" + "\n".join("  - " + f_ for f_ in fails)) + "\n")
    if "--no-render" in sys.argv:
        return
    renders(base, cover, foot, c, felt_heel, felt_rails, scr, pm, cam)


# ===================================================================== renders
def renders(base, cover, foot, c, felt_heel, felt_rails, scr, pm, cam):
    out = HERE
    SILVER, BLACK, FOOTC, FELT = "#c9c7c0", "#2a2c31", "#1d1f23", "#6d6a64"
    acc = COL["accent"]
    dimple = M.cylinder(0.12, 1.15).translate((PRG["cx"], PRG["cy"], H - 0.4))
    vis = [(cover, SILVER), (base, BLACK), (foot, FOOTC), (felt_heel, FELT), (felt_rails, FELT),
           (c["screen"], COL["screen"]), (c["bracket"], "#111318"), (dimple, acc)]
    title = f"{ID}  Rangefinder block (final)"
    pre = os.path.join(out, ID)
    pngs = []
    VIEWS["side"] = dict(pos=(1.0, 0.0, 0.0001), up=(0, 0, 1))
    VIEWS["rear"] = dict(pos=(0.0, -1.0, 0.0001), up=(0, 0, 1))
    pngs += render_views(vis, pre, views=("hero", "top", "side", "rear", "under"), title=title, ortho_views=("top", "side", "rear"))

    dz = 13
    board = union([c["pcb"], c["usb"], c["sockets"], c["switches"], c["leds"], c["top_parts"]])
    ex = [(cover.translate((0, 0, 3.0 * dz)), SILVER), (c["antenna"].translate((0, 0, 2.3 * dz)), acc),
          (board.translate((0, 0, 1.6 * dz)), COL["pcb"]), (c["bracket"].translate((0, 0, 1.6 * dz)), "#111318"),
          (c["screen"].translate((0, 0, 1.6 * dz)), COL["screen"]), (c["battery"].translate((0, 0, 0.8 * dz)), COL["battery"]),
          (c["light_pipe"].translate((0, 0, 2.6 * dz)), "#e8f4ff"),
          (base, BLACK), (scr.translate((0, 0, 0.5 * dz)), "#9aa0a8"),
          (foot.translate((0, 0, -0.9 * dz)), FOOTC), (felt_heel.translate((0, 0, -1.3 * dz)), FELT),
          (felt_rails.translate((0, 0, -0.9 * dz)), FELT)]
    pngs += render_views(ex, pre + "_exploded", views=("hero",), title=title + "  exploded")

    content_cols = [("pcb", COL["pcb"]), ("bracket", "#111318"), ("usb", "#9097a0"), ("sockets", "#d8c37a"),
                    ("switches", "#c8ccd2"), ("leds", acc), ("battery", COL["battery"]), ("antenna", acc), ("screen", COL["screen"]),
                    ("usb_overmold", "#5b6b8c"), ("light_pipe", "#e8f4ff"), ("sh_plug", "#e0e0e0"), ("sh_wire_bend", "#c0503a")]

    def cutset(n, off, crop=None, extra=()):
        parts = [(cover.trim_by_plane(n, off), "#a3a19a"), (base.trim_by_plane(n, off), BLACK), (foot.trim_by_plane(n, off), FOOTC)]
        for k, col in content_cols + list(extra):
            mm = c[k].trim_by_plane(n, off)
            if not mm.is_empty():
                parts.append((mm, col))
        if crop is not None:
            parts = [(p ^ crop, col) for p, col in parts]
            parts = [(p, col) for p, col in parts if not p.is_empty()]
        return parts

    ys = SNAP_YS[0]
    VIEWS["secy"] = dict(pos=(0.0, -1.0, 0.0001), up=(0, 0, 1))
    pngs += render_views(cutset((0, 1, 0), ys), pre + "_snap_section", views=("secy",), title=title + f"  section y={ys} through two snaps",
                         ortho_views=("secy",))
    crop = box(12.0, 21.5, ys - 5, ys + 5, 9.0, 22.0)
    pngs += render_views(cutset((0, 1, 0), ys, crop), pre + "_snap_zoom", views=("secy",),
                         title="snap: 0.8 arm, 0.5 root fillet, 0.30 interference, 45 deg lead-in and retain face, 0.55 relief", ortho_views=("secy",))
    pngs += render_views(cutset((0, 1, 0), RST_XY[1]), pre + "_buttons_section", views=("secy",),
                         title=title + "  section through PRG (right) and RST (left) switches", ortho_views=("secy",))
    crop2 = box(-14, 14, RST_XY[1] - 5, RST_XY[1] + 5, 13.5, 22)
    pngs += render_views(cutset((0, 1, 0), RST_XY[1], crop2), pre + "_buttons_zoom", views=("secy",),
                         title="buttons: RST tile 0.8 recessed (left), PRG disc flush (right), nubs 0.3 over the switches", ortho_views=("secy",))
    VIEWS["secx"] = dict(pos=(1.0, 0.0, 0.0001), up=(0, 0, 1))
    pngs += render_views(cutset((-1, 0, 0), 0.0), pre + "_usb_section", views=("secx",),
                         title="long section x=0: USB overmold (blue), SH1.25 plug (white) + wire bend (red), cell", ortho_views=("secx",))
    # antenna x-ray
    ghost = [(cover, SILVER, 0.16), (base, BLACK, 0.22), (c["antenna"], acc), (board, COL["pcb"]),
             (c["bracket"], "#111318"), (c["battery"], COL["battery"])]
    VIEWS["xray"] = dict(pos=(0.8, -1.0, 1.3), up=(0, 0, 1))
    pngs += render_views(ghost, pre + "_xray", views=("xray", "top"), title=title + f"  antenna {EST.ant_len:.0f} mm: service loop in the nose bay, then the groove (orange)",
                         ortho_views=("top",))
    VIEWS["secl"] = dict(pos=(0.0, -1.0, 0.0001), up=(0, 0, 1))
    crop3 = box(-20.5, -9, 30 - 3, 30 + 3, 6, 22)
    pngs += render_views(cutset((0, 1, 0), 30.0, crop3), pre + "_antenna_section", views=("secl",),
                         title="antenna lane section y=30: 1.9 bore, 45 deg 1.3 side-entry mouth, >= 1 mm plastic over the cell", ortho_views=("secl",))
    # coin slot: section across the right side wall through the slot centre, with the test coin in at 0.8 past the skin
    yc = (PRY_Y[0] + PRY_Y[1]) / 2
    coin = M.cylinder(COIN[1], COIN[0] / 2, COIN[0] / 2, 256).translate(
        (W2 - PROUD - COIN_REACH + COIN[0] / 2, yc, SPLIT - PRY[1] / 2 - COIN[1] / 2))
    crop4 = box(13.0, 26.0, yc - 8, yc + 8, 8.0, 17.0)
    pngs += render_views(cutset((0, 1, 0), yc, crop4) + [(coin.trim_by_plane((0, 1, 0), yc) ^ crop4, "#c08a4a")], pre + "_coin_slot",
                         views=("secy",), title=f"coin slot section y={yc:.0f}: 12.0 x 2.2 in the base, {PRY[2]} deep, cover whole; "
                         f"{COIN[0]} x {COIN[1]} coin {COIN_REACH} past the skin", ortho_views=("secy",))
    # print pose: the two plates as laid out in the two 3MFs (black left, silver right, shown 120 apart; bed area cropped)
    cols = dict(base=BLACK, cover=SILVER, foot=FOOTC)
    pp = []
    for k, (pk_, items) in enumerate(PLATES.items()):
        dx = -40.0 + 120.0 * k
        for (_, part, (cx, cy), _) in items:
            m = pm[part]
            b = m.bounding_box()
            pp.append((m.translate((dx + cx - (b[3] - b[0]) / 2, cy - (b[4] - b[1]) / 2, 0)), cols[part]))
        pp.append((box(dx + 80, dx + 195, 85, 171, -0.6, 0), "#5a5f66"))
    VIEWS["plate"] = dict(pos=(0.3, -1.0, 0.9), up=(0, 0, 1))
    pngs += render_views(pp, pre + "_print_pose", views=("plate",), title="print pose: black plate (base, foot on mouse ears) | silver plate (cover)")
    # on camera
    pl = cam_place()
    dev = [(m.translate(pl), col) for m, col in vis]
    ovm = c["usb_overmold"].translate(pl)
    cab = M.cylinder(40, EST.usb_cable_d / 2).rotate((90, 0, 0)).translate((0, REC_FACE_Y - EST.usb_env[2], USB_ZC)).translate(pl)
    camp = [(cam["lower"], "#1f2024"), (cam["upper"], "#c4c3be"), (cam["grip"], "#1f2024"), (cam["lens"], "#16171a"),
            (cam["evf"], "#1b1c1f"), (cam["eyecup"], "#111214"), (cam["lcd"], "#121316"), (cam["dials"], "#b8b7b2"), (cam["shoe"], "#8e8f92")]
    VIEWS["cam_rear"] = dict(pos=(0.35, -1.25, 0.45), up=(0, 0, 1))
    VIEWS["cam_back"] = dict(pos=(0.0, -1.0, 0.0001), up=(0, 0, 1))
    pngs += render_views(camp + dev + [(ovm, "#5b6b8c"), (cab, "#3a3f48")], pre + "_oncam", views=("hero", "cam_rear", "cam_back"),
                         title=title + "  on X-E5 proxy, USB-C cable plugged in", ortho_views=("cam_back",))
    contact_sheet(pngs, os.path.join(out, f"{ID}_sheet.png"), cols=4, cell=(800, 600), label=f"{ID}  Rangefinder block, final print-ready")
    print("\n".join(pngs))


if __name__ == "__main__":
    main()
