"""One-piece oval plunger, installed from inside revised case fronts.
Original Miniworks reference: Chem_Kid, Thingiverse 7367243, CC BY-NC.
"""
from pathlib import Path
import sys,numpy as np,manifold3d as M
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/'miniworks_integrated'));import model as shell
M.set_circular_segments(128)
box,cyl,slab,rrect=shell.box,shell.cyl,shell.slab,shell.rrect
REFERENCE=shell.BUTTON.transform(shell.BUTTON_R)
REFERENCE=REFERENCE.translate((0,0,-REFERENCE.bounding_box()[2]))
REST_Z=shell.C-1.524;R=2.286;RST_RECESS=.8
CAP_RISE=1.4;PRESS=1.2
FOOT_Z=19.65;RAMP_TOP=21.25
HEAD=rrect(-4.5,4.5,-2.65,3.75,2.5)   # USB redesign: trimmed on the USB-end side so the cap pocket stays clear of the end wall (full 1 mm wall behind the USB recess)
FLANGE=rrect(-6.1,6.1,-2.3,2.3,.8)
CH=.30
ramp=M.Manifold.batch_hull([slab(FLANGE,FOOT_Z,FOOT_Z+.001),slab(HEAD,RAMP_TOP-.001,RAMP_TOP)])
cap=slab(HEAD,RAMP_TOP-.001,shell.H+CAP_RISE-CH+.001)
cap=cap+M.Manifold.batch_hull([slab(HEAD,shell.H+CAP_RISE-CH,shell.H+CAP_RISE-CH+.001),slab(HEAD.offset(-CH,M.JoinType.Round,circular_segments=128),shell.H+CAP_RISE-.001,shell.H+CAP_RISE)])
ACTION=(cyl(0,0,REST_Z,FOOT_Z+.01,R)+ramp+cap).as_original().simplify(1e-5)
rf=REFERENCE^box(-5,5,-5,5,-.01,1.60)
rst_top=shell.H-RST_RECESS-REST_Z
RESET=rf+cyl(0,0,1.50,rst_top-.25,R)+cyl(0,0,rst_top-.25,rst_top,R,R-.25)
centres=[]
for p in shell.HB.slice(19.5).to_polygons():
 q=np.asarray(p);span=np.ptp(q,axis=0);c=(q.max(0)+q.min(0))/2
 if 4.9<span[0]<5.2 and 4.9<span[1]<5.2 and 1<c[1]<10:centres.append(c)
# Board orientation, from a photo of the Heltec V4: front facing viewer, USB left, OLED right,
# PRG above USB, RST below. In this right-handed CAD frame image up is -x;
# the action control therefore belongs at negative x, reset at positive x.
CENTRES=sorted(centres,key=lambda q:q[0]);ACTION_CENTRE,RESET_CENTRE=CENTRES
CX,CY=map(float,ACTION_CENTRE)
# Straight inner loading path for the oval head. Widen only the inside shoulder
# chamber for the retaining flange; the front-face aperture stays 9.6 x 8.1 mm.
HEAD_CLEAR=HEAD.offset(.30,M.JoinType.Round,circular_segments=128)
FLANGE_CLEAR=FLANGE.offset(.30,M.JoinType.Round,circular_segments=128)
clear_ramp=M.Manifold.batch_hull([slab(FLANGE_CLEAR,FOOT_Z,FOOT_Z+.001),slab(HEAD_CLEAR,RAMP_TOP-.001,RAMP_TOP)])
inside_clear=M.CrossSection.hull_points(np.concatenate(HEAD_CLEAR.to_polygons()+FLANGE_CLEAR.to_polygons()))
ramp_sweep=M.Manifold.batch_hull([clear_ramp,clear_ramp.translate((0,0,-(RAMP_TOP-FOOT_Z)))])
void=slab(HEAD_CLEAR,shell.S-.1,shell.H+.1)+slab(inside_clear,shell.S-.1,FOOT_Z+.001)+ramp_sweep
CUT=void.translate((CX,CY,0))
# Round variant: the oval CUT is NOT applied. The shoot position keeps model.py's original round
# hole (r 2.54, 45 degree countersink at the face), so the USB end wall behind it is untouched.
HB=shell.HB;CB=shell.CB
# Shoot plunger = same construction as RESET (reference foot with retaining tabs + round shaft r 2.286),
# but its top stands PROUD of the front face, with a 45 degree edge chamfer and a diamond knurl inside a smooth rim.
# Layer alignment: total height is 25 layers (5.0), tabs 7 layers (1.40) so they sit 0.12 below the ceiling,
# groove floor on the 4.6 layer boundary, coarser # knurl (pitch 1.4, grooves offset half a pitch), wider rim.
ACT_H=5.0;TAB_H=1.40;TOP_CH=.30;RIM=.70;K_PITCH,K_D=1.4,.4
act_top=ACT_H;PROUD=REST_Z+ACT_H-shell.H
rfa=REFERENCE^box(-5,5,-5,5,-.01,TAB_H)
_act=rfa+cyl(0,0,TAB_H-.1,act_top-TOP_CH,R)+cyl(0,0,act_top-TOP_CH,act_top,R,R-TOP_CH)
_tri=M.CrossSection([shell.ccw([(-K_D-.01,act_top+.01),(0,act_top-K_D),(K_D+.01,act_top+.01)])])
_g=_tri.extrude(12).transform(np.array([[1,0,0,0],[0,0,1,-6],[0,1,0,0]],float))
_gs=[_g.translate(((k+.5)*K_PITCH,0,0)).rotate((0,0,a))for a in(45,-45)for k in range(-3,3)]
KNURL=M.Manifold.batch_boolean(_gs,M.OpType.Add)^cyl(0,0,act_top-1,act_top+1,R-TOP_CH-RIM)
ACTION_LOCAL=(_act-KNURL).as_original().simplify(1e-5)
ACTION=ACTION_LOCAL.translate((0,0,REST_Z))
HA,CA=shell.HA,shell.CA
assembled={'handheld_back':HA,'handheld_front':HB,'camera_back':CA,'camera_front':CB,'action_button':ACTION,'reset_RST':RESET}
PRINT_R={n:shell.FLIP if n.endswith('front')else None for n in assembled}
def place(m,R=None):
 if R is not None:m=m.transform(R)
 return m.translate(-np.array(m.bounding_box()[:3]))
parts={n:place(m,PRINT_R[n])for n,m in assembled.items()}
STL_NAMES={n:'miniworks_onepiece_corrected_'+n+'.stl'for n in parts}
def tm(m):return shell.tm(m)
def unpose(m,name):
 p=assembled[name];R=PRINT_R[name];p=p.transform(R)if R is not None else p
 m=m.translate(p.bounding_box()[:3]);return m.transform(R)if R is not None else m
def action(press=0):return ACTION.translate((CX,CY,-press))
def reset(press=0):return RESET.translate((RESET_CENTRE[0],RESET_CENTRE[1],REST_Z-press))
