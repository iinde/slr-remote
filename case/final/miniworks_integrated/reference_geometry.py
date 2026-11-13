"""Miniworks XE5 correction: preserve source details, make the LP603449 fit,
close the handheld GPS end, and provide a usable slide-on camera shoe mount.

Changes: M2 bosses and clearance holes; 0.25 internal floor bevel; 1.27 far-end
underside bevel to retain a real floor; continuous handheld end; 1.6 camera-back
slab with a 45-degree skirt, open dovetail entrance, and flexible detent leaf.
Original designer: Chem_Kid, Thingiverse 7367243, CC BY-NC.

Imported by model.py; the checks run from final/miniworks_refined/validate.py.
Frame: x across, y from USB end, z from back; exact source seam 10.7919998.
"""
import os, sys, zipfile, importlib.util, json, io, hashlib
from pathlib import Path
import numpy as np, trimesh, manifold3d as M

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from mw_load import MA, MB  # noqa: E402

FAILS = []
def check(ok, msg):
    print(("PASS " if ok else "FAIL ") + msg)
    if not ok: FAILS.append(msg)

def rbox(x0, x1, y0, y1, z0, z1, r):
    cs = M.CrossSection.square((x1 - x0 - 2 * r, y1 - y0 - 2 * r)).translate((x0 + r, y0 + r)).offset(r, M.JoinType.Round, circular_segments=64)
    return cs.extrude(z1 - z0).translate((0, 0, z0))
def box(x0, x1, y0, y1, z0, z1):
    return M.Manifold.cube((x1 - x0, y1 - y0, z1 - z0)).translate((x0, y0, z0))
def cyl(x, y, z0, z1, r):
    return M.Manifold.cylinder(z1 - z0, r, r, 48).translate((x, y, z0))
def prism_x(pts_yz, x0, x1):   # polygon in (y, z), extruded along x
    return M.CrossSection([pts_yz]).extrude(x1 - x0).transform(np.array([[0, 0, 1, x0], [1, 0, 0, 0], [0, 1, 0, 0]], float))
def prism_y(pts_xz, y0, y1):   # polygon in (x, z), extruded along y
    return M.CrossSection([pts_xz]).extrude(y1 - y0).transform(np.array([[1, 0, 0, 0], [0, 0, 1, y0], [0, 1, 0, 0]], float))

# ------------------------------------------------------------------ measured original
W, L, H, SEAM = 22.86, 55.87990188598633, MB.bounding_box()[5], MA.bounding_box()[5]      # half width, main body length, height, seam
CH = 3.79                                         # 45 deg edge chamfer
BAY = (-20.33, 20.33, 2.54, 54.82, 2.53, 19.05)
CELL = (51.5, 35.0, 6.6)                          # LP603449 keep-out (+0.5 L/W, 0.3 swell)
BOARD = (52.0, 25.0, 12.93, 14.53)               # V4 PCB, inferred z
# ------------------------------------------------------------------ design
SCREW_X, SCREW_Y = 20.3, (15.24, 40.64)          # M2, in the side wall
BOSS_R, PILOT_D, CLEAR_D = 2.65, 1.8, 2.4
SLAB = 1.6                                        # camera back thickening
DT = dict(mouth=10.0, roof=12.0, depth=2.6, side=0.15, top=0.2)   # dovetail
DET = dict(h=0.3, ramp=1.2, flat=0.4)            # click bump on the tenon

def hull(m): return M.Manifold.batch_hull([m])
HULL_A, HULL_B = hull(MA), hull(MB)
OLD_X = 19.05                                     # original M3 holes; their ears are reused for the M2 heads
FLOOR_Z, CEIL_Z = 2.5369999408721924, 19.047000885009766                   # measured
_loops = [np.asarray(q) for q in MA.slice(6.6).to_polygons()]          # z 6.6: above every floor chamfer, below the end openings (6.68)
_bay_loop = min(_loops, key=lambda q: np.ptp(q[:, 0]) * np.ptp(q[:, 1]) if np.ptp(q[:, 0]) > 30 else 1e9)
_ym = (2.5398 + 54.817) / 2
_half = _bay_loop[_bay_loop[:, 1] < _ym]           # USB half: clean corners (the far end has the SMA block and GPS bevels)
_pts = np.vstack([_half, np.c_[_half[:, 0], 2 * _ym - _half[:, 1]]])
def _chull(P):                                    # monotone chain, counter-clockwise
    P = sorted(set(map(tuple, np.round(P, 5))))
    cr = lambda o, a, b: (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lo, up = [], []
    for p in P:
        while len(lo) >= 2 and cr(lo[-2], lo[-1], p) <= 0: lo.pop()
        lo.append(p)
    for p in reversed(P):
        while len(up) >= 2 and cr(up[-2], up[-1], p) <= 0: up.pop()
        up.append(p)
    return lo[:-1] + up[:-1]
BAY_CS = M.CrossSection([_chull(_pts)])            # original walls and corner facets, both ends; lobes and blocks filled in
FCH = 0.25                                        # floor chamfer, was 1.9: the cell keep-out has 0.39 spare at each end
# A only (the cell lives below the seam): the bay with a 0.4 x 45 floor chamfer all round, walls exactly the original's
CUT_A = M.Manifold.batch_hull([BAY_CS.offset(-FCH, M.JoinType.Round, circular_segments=64).extrude(0.001).translate((0, 0, FLOOR_Z)),
                               BAY_CS.extrude(SEAM + 0.02 - FLOOR_Z - FCH).translate((0, 0, FLOOR_Z + FCH))])
BAY_ALL = BAY_CS.extrude(H + 2 - FLOOR_Z).translate((0, 0, FLOOR_Z))   # above the floor only
# keep the block behind the SMA socket beyond y 54.45 (the cell ends at 54.43): it closes the SMA box off from the bay
# No partial obstruction behind the SMA: the entire padded cell must clear.
ZMIN = MA.bounding_box()[2]
# Removing the old floor bevel exposes the original outside bevel. Restore a real
# floor by reducing only the far-end underside bevel to the original 1.27 side bevel.
# The plan outline and all upper SMA/GPS detail remain original.
BACK_END_CHAMFER = 1.27
outer_loop=max(_loops,key=lambda q: np.ptp(q[:,0])*np.ptp(q[:,1]))
clean_outer=outer_loop[outer_loop[:,1]<(L-0.0001)/2]
main_plan=M.CrossSection.hull_points(np.vstack([clean_outer,np.c_[clean_outer[:,0],L-0.0001-clean_outer[:,1]]]))
reinforce = M.Manifold.batch_hull([
    main_plan.offset(-BACK_END_CHAMFER, M.JoinType.Round, circular_segments=80).extrude(0.001).translate((0,0,ZMIN)),
    main_plan.extrude(3.806999921798706-ZMIN-BACK_END_CHAMFER).translate((0,0,ZMIN+BACK_END_CHAMFER))
]) ^ box(-30,30,51.0,L+0.1,ZMIN-0.01,3.806999921798706)


def base(half):
    isA = half is MA
    lo, hi = (3.806999921798706, SEAM) if isA else (SEAM, H)
    # Close the WHOLE old drilling, including its mouth chamfer, before moving it.
    fills = M.Manifold.batch_boolean([cyl(sx*OLD_X,y,lo,hi,2.0) for sx in (-1,1) for y in SCREW_Y], M.OpType.Add)
    m = half + (fills ^ half.hull())
    if isA:
        m = (m + reinforce) - CUT_A
        bosses = M.Manifold.batch_boolean([cyl(sx*SCREW_X,y,FLOOR_Z,SEAM,BOSS_R) for sx in (-1,1) for y in SCREW_Y], M.OpType.Add)
        m = m + (bosses ^ half.hull())
    radius = PILOT_D/2 if isA else CLEAR_D/2
    holes = M.Manifold.batch_boolean([cyl(sx*SCREW_X,y,lo,hi+0.1,radius) for sx in (-1,1) for y in SCREW_Y],M.OpType.Add)
    return m - holes

A0, B0 = base(MA), base(MB)
BOSS_A = M.Manifold.batch_boolean([cyl(sx*SCREW_X,y,FLOOR_Z,SEAM,BOSS_R) for sx in (-1,1) for y in SCREW_Y], M.OpType.Add) ^ MA.hull()
PILOTS = M.Manifold.batch_boolean([cyl(sx*SCREW_X,y,3.806999921798706,SEAM+0.1,PILOT_D/2) for sx in (-1,1) for y in SCREW_Y],M.OpType.Add)

# ------------------------------------------------------------------ handheld: close the GPS end as one complete shell
GPS_OFF = box(-4.94,30,L,90,-5,30)
whole = MA + MB
section_x = np.array([[0,1,0,0],[0,0,1,0],[1,0,0,0]],float)
section_y = np.array([[1,0,0,0],[0,0,1,0],[0,1,0,0]],float)
def extrude_x(cs,x0,x1):
    return cs.extrude(x1-x0).transform(np.array([[0,0,1,x0],[1,0,0,0],[0,1,0,0]],float))
def extrude_y(cs,y0,y1):
    return cs.extrude(y1-y0).transform(np.array([[1,0,0,0],[0,0,1,y0],[0,1,0,0]],float))
# Use the original end section for the outside slope and upper inner chamfer.
# Supply the full top plane: the centre section passes through the screen window.
prof = whole.transform(section_x).slice(-1.0)
endprof = prof ^ M.CrossSection.square((L-48.98,30)).translate((48.98,-2))
outer_prof = M.CrossSection.hull_points(np.vstack([np.concatenate(endprof.to_polygons()),[[48.98,ZMIN],[48.98,H],[51.0,ZMIN],[51.0,H]]]))
corner_outer = (whole ^ box(12.2,30,-1,6.9,-1,30)).hull().mirror((0,1,0)).translate((0,L-0.0001,0))
corner_stock = extrude_x(outer_prof,12.2,W+0.1) ^ corner_outer
flat_prof = outer_prof ^ M.CrossSection.square((L-51.1,30)).translate((51.1,-2))
flat_stock = extrude_x(flat_prof,-4.94,12.21)
# Intersect the empty end and side sections, keeping the original upper bevels.
endvoid = (outer_prof-prof) ^ M.CrossSection.square((L-48.98,CEIL_Z-SEAM)).translate((48.98,SEAM))
sideprof = whole.transform(section_y).slice(28.0)
side_hull = M.CrossSection.hull_points(np.concatenate(sideprof.to_polygons()))
sidevoid = (side_hull-sideprof) ^ M.CrossSection.square((41,CEIL_Z-SEAM)).translate((-20.5,SEAM))
uppervoid = extrude_x(endvoid,-30,30) ^ extrude_y(sidevoid,0,90) ^ BAY_CS.extrude(30).translate((0,0,-1))
END_STOCK = (corner_stock+flat_stock)-uppervoid
# Deliberate overlaps at every joining plane avoid coincident slivers.
END_REGION = box(12.22,30,49.0,90,-1,30)+box(-4.94,12.22,51.12,90,-1,30)
HA = ((((A0-END_REGION)+(END_STOCK ^ box(-30,30,0,90,-1,SEAM))+reinforce)-GPS_OFF)-CUT_A)+BOSS_A-PILOTS
HB = ((B0-END_REGION)+(END_STOCK ^ box(-30,30,0,90,SEAM,H+1)))-GPS_OFF
# Floating zero-volume faces can result from exact coplanar source facets. Simplify
# below STL float precision; no legitimate component may be silently discarded.
HA, HB = HA.simplify(1e-6), HB.simplify(1e-6)

# Shared layer scanner, imported without constructing or exporting a mount.
spec=importlib.util.spec_from_file_location("cb",os.path.join(HERE,"..","camera_block","build.py"))
cb=importlib.util.module_from_spec(spec);sys.path.insert(0,os.path.dirname(spec.origin));spec.loader.exec_module(cb)
