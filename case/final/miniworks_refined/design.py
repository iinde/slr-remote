"""Final USB/bevel refinement; existing seam, corrected controls and inset cord bridge.

Original Miniworks by Chem_Kid, Thingiverse 7367243, CC BY-NC.
The lower USB wall belongs to the back as a tapered saddle. Its side joints
follow the flare rather than rectangular notches, while the front stays open
in its face-down print pose. All visible new edge treatments are 45 degrees.
"""
from pathlib import Path
import sys,importlib.util,numpy as np,manifold3d as M
HERE=Path(__file__).resolve().parent
BASE=HERE.parent/'miniworks_lanyard_integrated'
spec=importlib.util.spec_from_file_location('lanyard_base',BASE/'design.py')
lanyard=importlib.util.module_from_spec(spec);spec.loader.exec_module(lanyard)
base=lanyard.base;shell=base.shell
M.set_circular_segments(128)
box,cyl,slab=shell.box,shell.cyl,shell.slab
S,H=shell.S,shell.H
# Same connector/cable limits as before, with tangent rounded corners in place
# of the eight-sided outer mouth. This cut contains the former flare.
# Chunky version: a straight-walled pocket the size of the plug, with a 0.4 mm bevel at the mouth. No taper,
# so the wall behind it is a flat 1 mm plate with the port cut in it (2.2 mm frame at the sides, 1.6 mm above).
REC=shell.rrect(-6.7,6.7,13.4,19.7,1.5)
USB_RECESS=shell.xz_shape(REC,-.02,1.502)+M.Manifold.batch_hull([shell.xz_shape(REC.offset(.4,M.JoinType.Round,circular_segments=64),-.02,-.019),shell.xz_shape(REC,.4,.401)])
# ---- USB joint: every wall here is at least 0.8 mm.
# The back owns a plain block (the saddle) under the port, wider than the plug recess, so the whole lower
# half of the recess sits inside it with a 0.9 mm shoulder each side. The front has a matching open notch.
USB_HW=7.6
USB_LOCAL=box(-USB_HW-.7,USB_HW+.7,-.03,2.74,S-2e-4,15.25)
saddle_profile=[(-USB_HW,S-.04),(USB_HW,S-.04),(USB_HW,15.03),(-USB_HW,15.03)]
slot_profile=[(-USB_HW-.2,S-.10),(USB_HW+.2,S-.10),(USB_HW+.2,15.23),(-USB_HW-.2,15.23)]
SADDLE=shell.prism_y(saddle_profile,-.02,2.5398)^shell.OUTER['camera']
# Battery-wire pocket inside the saddle: original height, closed by a 45 degree roof (no bridge, no cut-through).
WIRE_POCKET=box(-10.04,-5.21,1.27,2.59,S-.1,12.32)+shell.prism_x([(1.27,12.31),(2.59,12.31),(2.59,12.32+1.32)],-10.04,-5.21)
SADDLE=SADDLE-USB_RECESS-shell.USB_PORT-WIRE_POCKET
SLOT=shell.prism_y(slot_profile,-.1,2.74)
# The receptacle opening runs on through the inner lip, so no sliver is left between it and the cap pocket.
PORT_THROUGH=shell.xz_shape(shell.rrect(-4.5,4.5,15.03,18.03,1.2),2.5,4.0)
# Square lower corners on the port in the front half: straight legs down to the notch, no thin hooks.
PORT_SQUARE=box(-4.5,4.5,1.4,4.0,15.0,16.6)

# Round variant: no action bevel is added; the original hole already has its 45 degree countersink.
ACTION_BEVEL=M.Manifold()
# Longer guide for the proud plunger. The wide countersink dish at the SHOOT hole is filled
# and the bore (same r 2.54) re-cut to the face with only a 0.5 mm 45 degree lead chamfer.
_cx,_cy=base.CX,base.CY;BORE_R=2.54;BORE_CH=.5
ACTION_FILL=cyl(_cx,_cy,20.0,H,3.85)
ACTION_BORE=cyl(_cx,_cy,shell.C-.2,H+.01,BORE_R)+cyl(_cx,_cy,H-BORE_CH,H+.001,BORE_R,BORE_R+BORE_CH+.001)
ACTION_EDIT=cyl(_cx,_cy,shell.C-.3,H+.1,3.95)
def refine(a,b):
 a=((a-USB_LOCAL-USB_RECESS)+SADDLE).as_original().simplify(1e-5)
 b=(((b+ACTION_FILL)^(b+shell.box(-40,40,-5,100,0,H)))-ACTION_BORE-SLOT-USB_RECESS-PORT_THROUGH-PORT_SQUARE).as_original().simplify(1e-5)
 return a,b
HA,HB=refine(lanyard.BACK,base.HB)
CA,CB=refine(base.CA,base.CB)
# Round variant: the shoot plunger comes complete (chamfer + knurl) from the base layer.
CH=base.TOP_CH;CAP_TOP=H+base.PROUD
ACTION=base.ACTION
RESET=base.RESET
ACTION_CENTRE,RESET_CENTRE=base.ACTION_CENTRE,base.RESET_CENTRE
assembled={'handheld_back':HA,'handheld_front':HB,'camera_back':CA,'camera_front':CB,'action_button':ACTION,'reset_RST':RESET}
PRINT_R={n:shell.FLIP if n.endswith('front')else None for n in assembled}
def place(m,R=None):
 if R is not None:m=m.transform(R)
 return m.translate(-np.array(m.bounding_box()[:3]))
parts={n:place(m,PRINT_R[n])for n,m in assembled.items()}
STL_NAMES={n:'miniworks_refined_'+n+'.stl'for n in parts}
def tm(m):return shell.tm(m)
def unpose(m,n):
 p=assembled[n];R=PRINT_R[n];p=p.transform(R)if R is not None else p
 m=m.translate(p.bounding_box()[:3]);return m.transform(R)if R is not None else m
def action(d=0):return ACTION.translate((base.CX,base.CY,-d))
def reset(d=0):return RESET.translate((RESET_CENTRE[0],RESET_CENTRE[1],base.REST_Z-d))
