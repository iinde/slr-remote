"""Recessed lanyard tunnel within the USB-end corner; no protruding eyelet.

Corrected handheld front remains unchanged. The corner wall is thickened only
inside, outside the complete padded cell envelope. Cord loops around the solid
corner bridge through two external ports. Teardrop roofs print without support.
Original Miniworks: Chem_Kid, Thingiverse 7367243, CC BY-NC.
"""
from pathlib import Path
import sys,importlib.util,numpy as np,manifold3d as M
HERE=Path(__file__).resolve().parent;BASE=HERE.parent/'miniworks_onepiece_corrected'
spec=importlib.util.spec_from_file_location('corrected_base',BASE/'design.py')
base=importlib.util.module_from_spec(spec);spec.loader.exec_module(base)
shell=base.shell
M.set_circular_segments(128)
box,cyl,slab=shell.box,shell.cyl,shell.slab
X0,YB,ZC=20.0,4.5,4.8
BORE_R,BEND_R=1.25,2.5
FILL_X=17.65
# The original perimeter is retained exactly. This inboard corner fill remains
# 0.15 mm beyond the padded cell, and tapers back into the original side wall.
fill_plan=shell.rrect(FILL_X,shell.W+.02,-1,9.8,1.0)
stock=slab(fill_plan,-1,9.5)^shell.OUTER['handheld'].translate((-.2,.2,.2))  # stay 0.2 inside the wall, never coincident with the outer faces
inboard_relief=shell.prism_y([(FILL_X-2,6.8),(FILL_X,6.8),(FILL_X+2.7,9.5),(FILL_X-2,9.5)],-2,12)
STOCK=stock-inboard_relief

# Circular bore with a tangent 45 degree pointed roof. Its full 2.5 mm circular
# core remains open, with extra height above it to close each layer gradually.
arc=np.linspace(3*np.pi/4,9*np.pi/4,97)
profile=np.c_[BORE_R*np.cos(arc),BORE_R*np.sin(arc)]
profile=np.vstack([profile,[0,BORE_R*np.sqrt(2)]])
CS=M.CrossSection([shell.ccw(profile)])
def section(px,py,tx,ty):
 nx,ny=-ty,tx
 return CS.extrude(.02).transform(np.array([[nx,0,tx,px-.01*tx],[ny,0,ty,py-.01*ty],[0,1,0,ZC]],float))

# Start and finish are outside the shell. The rounded L path connects the
# rounded USB-end face to the neighbouring side face of the same corner.
points=[(X0,-2,0,1),(X0,YB,0,1)]
for a in np.linspace(np.pi,np.pi/2,25)[1:]:
 points.append((X0+BEND_R+BEND_R*np.cos(a),YB+BEND_R*np.sin(a),np.sin(a),-np.cos(a)))
points.append((25,YB+BEND_R,1,0))
sections=[section(*q)for q in points]
CHANNEL=M.Manifold.batch_boolean([M.Manifold.batch_hull([a,b])for a,b in zip(sections,sections[1:])],M.OpType.Add)
BACK=((base.HA+STOCK)-CHANNEL).as_original().simplify(1e-5)
FRONT=base.HB
LOCAL=box(16.4,23.1,-.1,10.1,-.01,9.6)
assembled={'handheld_back':BACK,'handheld_front':FRONT}
PRINT_R={'handheld_back':None,'handheld_front':shell.FLIP}
def place(m,R=None):
 if R is not None:m=m.transform(R)
 return m.translate(-np.array(m.bounding_box()[:3]))
parts={n:place(m,PRINT_R[n])for n,m in assembled.items()}
STL_NAMES={'handheld_back':'miniworks_handheld_back_inset_lanyard.stl','handheld_front':'miniworks_handheld_front_corrected.stl'}
def tm(m):return shell.tm(m)
def unpose(m,name):
 p=assembled[name];R=PRINT_R[name];p=p.transform(R)if R is not None else p
 m=m.translate(p.bounding_box()[:3]);return m.transform(R)if R is not None else m
