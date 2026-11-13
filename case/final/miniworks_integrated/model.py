"""Integrated Miniworks shell: continuous exterior, no screw ears or mount.
Camera back stays flat. All print faces and local overhangs are designed for
0.20 mm support-free printing. Original functional locations are retained.
"""
from pathlib import Path
import sys,numpy as np,trimesh,manifold3d as M
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
import reference_geometry as ref
M.set_circular_segments(128)
W,L,H,S,F,C=22.86,55.88,float(ref.H),float(ref.SEAM),float(ref.FLOOR_Z),float(ref.CEIL_Z)
Z0=0.0
EDGE,EDGE_RISE=.65,.72
# Rolled back edge: starts at 45 degrees on the bed, then a tangent arc into the side wall (no hard line).
# Inset and height both scale up on the long sides, so the slope never drops below 45 degrees.
BACK_FLAT,BACK_R,SIDE_SCALE,ARC_N=1.2,4.0,1.3,12
def roll_profile():
 i0=BACK_FLAT+BACK_R*(1-np.cos(np.pi/4));z1=BACK_FLAT+BACK_R*np.sin(np.pi/4)
 pts=[(i0,0.0),(i0-BACK_FLAT,BACK_FLAT)]
 for phi in np.linspace(np.pi/4,0,ARC_N+1)[1:]:pts.append((BACK_R*(1-np.cos(phi)),z1-BACK_R*np.sin(phi)))
 return pts
# Grip band: fine 45 degree V grooves on both long sides, centred on the seam so the split line is one of them.
GRIP_D,GRIP_PITCH,GRIP_N,GRIP_Y=.4,1.3,2,(13.5,47.5)
END_Y=60.96     # body runs full width to the antenna end; no separate antenna nub
SCREW_X,SCREW_Y=19.8,(11.5,44.5)
BOSS_R,PILOT_R,CLEAR_R=2.15,.9,1.2
HEAD_R,HEAD_DEPTH,SEAT_R=1.95,2.2,1.75
PILOT_BOTTOM=2.7
ROOT_R=2.2
SEG=128

def ccw(pts):
 p=np.asarray(pts,float)
 return p if np.sum(p[:,0]*np.roll(p[:,1],-1)-p[:,1]*np.roll(p[:,0],-1))>0 else p[::-1]
def box(x0,x1,y0,y1,z0,z1):return M.Manifold.cube((x1-x0,y1-y0,z1-z0)).translate((x0,y0,z0))
def cyl(x,y,z0,z1,r0,r1=None,n=96):return M.Manifold.cylinder(z1-z0,r0,r0 if r1 is None else r1,n).translate((x,y,z0))
def slab(cs,z0,z1):return cs.extrude(z1-z0).translate((0,0,z0))
def prism_x(pts,x0,x1):return M.CrossSection([ccw(pts)]).extrude(x1-x0).transform(np.array([[0,0,1,x0],[1,0,0,0],[0,1,0,0]],float))
def prism_y(pts,y0,y1):return M.CrossSection([ccw(pts)]).extrude(y1-y0).transform(np.array([[1,0,0,0],[0,0,1,y0],[0,1,0,0]],float))
def rrect(x0,x1,y0,y1,r):return M.CrossSection.square((x1-x0-2*r,y1-y0-2*r)).translate((x0+r,y0+r)).offset(r,M.JoinType.Round,circular_segments=SEG).simplify(1e-5)
def xz_shape(cs,y0,y1):return cs.extrude(y1-y0).transform(np.array([[1,0,0,0],[0,0,1,y0],[0,1,0,0]],float))

def integrated_plan(gps):
 plan=rrect(-W,W,0,END_Y,6.35)
 if gps:plan=plan+rrect(2.54,W,30,81.28,6.35)
 # One continuous plan, with tangent concave root blends, before any surfaces
 # are made. No independent pedestal or cap is added to an existing shell.
 return plan.offset(ROOT_R,M.JoinType.Round,circular_segments=SEG).offset(-ROOT_R,M.JoinType.Round,circular_segments=SEG).simplify(2e-5)

def outer_shell(plan):
 # A concave outline must NOT be batch_hull'ed: that would fill the antenna
 # notch. Corresponding offset vertices create genuine continuous side faces.
 q=ccw(plan.to_polygons()[0]);n=len(q)
 e0=q-np.roll(q,1,axis=0);e0/=np.linalg.norm(e0,axis=1)[:,None]
 e1=np.roll(q,-1,axis=0)-q;e1/=np.linalg.norm(e1,axis=1)[:,None]
 n0=np.c_[-e0[:,1],e0[:,0]];n1=np.c_[-e1[:,1],e1[:,0]]
 mit=(n0+n1)/(1+(n0*n1).sum(axis=1))[:,None]
 inner=q+EDGE*mit
 nv=n0+n1;nv/=np.linalg.norm(nv,axis=1)[:,None]
 sc=1+(SIDE_SCALE-1)*nv[:,0]**2
 rings=[np.c_[q+(sc*i)[:,None]*mit,sc*z]for i,z in roll_profile()]
 rings+=[np.c_[q,np.full(n,H-EDGE_RISE)],np.c_[inner,np.full(n,H)]]
 verts=np.vstack(rings);nr=len(rings)
 tris=[]
 caps_b=np.asarray(M.triangulate([rings[0][:,:2]],epsilon=1e-7,allow_convex=False))
 caps=np.asarray(M.triangulate([inner],epsilon=1e-7,allow_convex=False))
 tris.extend(caps_b[:,::-1].tolist());tris.extend((caps+(nr-1)*n).tolist())
 for layer in range(nr-1):
  for i in range(n):
   j=(i+1)%n;a=layer*n+i;bb=layer*n+j;c=(layer+1)*n+j;d=(layer+1)*n+i
   tris.extend([[a,bb,c],[a,c,d]])
 m=M.Manifold(M.Mesh(np.asarray(verts,np.float32),np.asarray(tris,np.uint32)))
 if m.status()!=M.Error.NoError:raise RuntimeError('Invalid integrated loft '+str(m.status()))
 return m

BAY=rrect(-20.32,20.32,2.5398,54.817,3.81)
FCH,CCH=0.0,2.33
bay=slab(BAY,F,C-CCH+.001)+M.Manifold.batch_hull([slab(BAY,C-CCH,C-CCH+.001),slab(BAY.offset(-CCH,M.JoinType.Round,circular_segments=SEG),C-.001,C)])
inside=slab(BAY,F-.01,C+.01)
far_ceil=prism_x([(70.064-C-.5,C+.5),(60,70.064-60),(60,C+.5)],-30,30)^inside
relief=prism_x([(53.74,16.32),(53.74,17.40),(52.47,18.67),(51.40,18.67)],-5.08,5.08)
sma_back=box(-16.5,-9.3,53.34,54.827,14.60,C)^inside
boss_cs=M.CrossSection()
for sx in [-1,1]:
 for y in SCREW_Y:boss_cs=boss_cs+M.CrossSection.circle(BOSS_R,96).translate((sx*SCREW_X,y))

# Exact screen and button recesses from the original; the exterior is rebuilt.
original=ref.MA+ref.MB
window_box=box(-10,10,16.5,51.15,C-.01,H+.1)
WINDOW_VOID=window_box-original
BUTTON_VOID=M.Manifold()
for sx in [-1,1]:
 region=box(sx*8.25-4.0,sx*8.25+4.0,1.6,9.7,16.4,H+.1)
 BUTTON_VOID=BUTTON_VOID+(region-original)
# New USB recess is a controlled, print-friendly eight-sided flare.
def octagon(x0,x1,z0,z1,c):return M.CrossSection([ccw([(x0+c,z0),(x1-c,z0),(x1,z0+c),(x1,z1-c),(x1-c,z1),(x0+c,z1),(x0,z1-c),(x0,z0+c)])])
rec_out=xz_shape(octagon(-6.7,6.7,13.4,19.7,1.5),-.02,-.019)
rec_in=xz_shape(octagon(-5.2,5.2,14.9,18.2,.6),1.5,1.501)
USB_RECESS=M.Manifold.batch_hull([rec_out,rec_in])
USB_PORT=xz_shape(rrect(-4.5,4.5,15.03,18.03,1.2),-.1,2.64)
WIRE_CHANNEL=box(-10.04,-5.21,1.27,2.59,S-.01,12.32)
SMA_PASS=M.Manifold.batch_hull([box(-15.83,-6.56,53,53.001,6.98,14.60),box(-15.24,-7.59,57.149,57.15,6.98,14.60)])
SMA_NUT=box(-15.88,-6.99,57.15,59.69,5.46,16.13)
SMA_BARREL=M.Manifold.cylinder(2.0,5.97/2,5.97/2,128).rotate((-90,0,0)).translate((-11.43,59.5,S))
COMMON_VOID=WINDOW_VOID+BUTTON_VOID+USB_RECESS+USB_PORT+WIRE_CHANNEL+SMA_PASS+SMA_NUT+SMA_BARREL
GPS_VOID=box(4.7,20.7,54,75.14,7.62,14.6)+M.Manifold.batch_hull([slab(rrect(4.7,20.7,54,78.74,3.81),9.94,S+.01),slab(rrect(4.7,20.7,54,76.42,3.81),7.62,7.621)])+box(4.7,20.7,54,63.71,7.11,7.621)

# Permanent hollow galleries lighten the full-depth antenna shell. Their roofs
# close at 45 degrees; each gallery opens into the main internal bay.
def gallery(x,half,zbase,up,y0,y1):
 pts=[(x-half,zbase),(x+half,zbase),(x,zbase+half)]if up else[(x-half,zbase),(x,zbase-half),(x+half,zbase)]
 return prism_y(pts,y0,y1)
HOLLOW_SMA=gallery(-11.43,1.4,F,True,52.5,59.2)+gallery(-11.43,2.1,H-1.8,False,51.8,59.2)
HOLLOW_GPS=M.Manifold()
for x in [8.6,16.8]:HOLLOW_GPS=HOLLOW_GPS+gallery(x,2.8,F,True,53.6,77.2)
# The GPS cover is a thin shell, with two retaining rails left at the original
# module height, rather than a solid front cap.
HOLLOW_GPS=HOLLOW_GPS+slab(rrect(5.9,19.5,54.3,75.0,1.2),14.59,H-1.8)

cell_y0=(2.5398+54.817)/2-51.5/2
cell=box(-17.5,17.5,cell_y0,cell_y0+51.5,F,F+6.6)
board=box(-12.5,12.5,2.54,54.54,12.93,14.53)

def grip_groove(sx,zc,y0,y1,d):
 X=sx*W;pts=[(X+sx*.05,y0,zc),(X+sx*.05,y1,zc)]
 for y in(y0+2.5*d,y1-2.5*d):pts+=[(X+sx*.05,y,zc-d-.05),(X+sx*.05,y,zc+d+.05),(X-sx*d,y,zc)]
 return M.Manifold.hull_points(np.asarray(pts,float))
GRIP=M.Manifold.batch_boolean([grip_groove(sx,S+k*GRIP_PITCH,GRIP_Y[0],GRIP_Y[1],GRIP_D)for sx in(-1,1)for k in range(-GRIP_N,GRIP_N+1)],M.OpType.Add)
PLAN={};OUTER={};GROOVE={};TONGUE={};FEATURES={}
def build_case(gps):
 name='camera'if gps else'handheld'
 plan=integrated_plan(gps);outer=outer_shell(plan)
 # The internal wall and screw roots are one continuous cavity profile.
 base_bay=BAY-boss_cs
 rounded_bay=base_bay.offset(-.55,M.JoinType.Round,circular_segments=128).offset(.55,M.JoinType.Round,circular_segments=128).simplify(1e-5)
 root_zones=M.CrossSection()
 for sx in [-1,1]:
  for y in SCREW_Y:root_zones=root_zones+M.CrossSection.circle(3.5,96).translate((sx*SCREW_X,y))
 clear_bay=base_bay-((base_bay-rounded_bay)^root_zones)
 main_void=bay^slab(clear_bay,F-.1,H+.1)
 allvoid=COMMON_VOID+(GPS_VOID if gps else M.Manifold())+HOLLOW_SMA+(HOLLOW_GPS if gps else M.Manifold())
 body=(outer-main_void)+(far_ceil-relief)+sma_back
 body=body-allvoid-GRIP
 a=body^box(-40,40,-1,90,-.01,S)
 b=body^box(-40,40,-1,90,S,H+.1)
 for sx in [-1,1]:
  for y in SCREW_Y:
   x=sx*SCREW_X
   a=a-cyl(x,y,PILOT_BOTTOM,S+.1,PILOT_R)
   b=b-cyl(x,y,S-.1,H+.1,CLEAR_R)
   b=b-cyl(x,y,H-HEAD_DEPTH,H+.1,HEAD_R)
   b=b-cyl(x,y,H-HEAD_DEPTH-.60,H-HEAD_DEPTH,CLEAR_R,SEAT_R)
 # Raised internal locating rim. Its four screw zones and all ports are clear.
 lip_cs=BAY.offset(.95,M.JoinType.Round,circular_segments=128)-BAY.offset(.15,M.JoinType.Round,circular_segments=128)
 groove_cs=BAY.offset(1.10,M.JoinType.Round,circular_segments=128)-BAY
 tongue=slab(lip_cs,S-.02,S+.8)^outer
 groove=slab(groove_cs,S-.03,S+.96)^outer
 for sx in [-1,1]:
  for y in SCREW_Y:
   zone=cyl(sx*SCREW_X,y,S-.1,S+1.1,3.3)
   tongue=tongue-zone;groove=groove-zone
 tongue=tongue-allvoid
 a=a+tongue;b=b-groove
 # The lower USB wall belongs to the back; front print pose has an open notch.
 take=box(-5.32,5.32,-.02,2.5399,S-.01,14.93)
 saddle=body^take;a=a+saddle
 b=b-box(-5.5,5.5,-.1,2.72,S-.1,15.12)
 PLAN[name]=plan;OUTER[name]=outer;GROOVE[name]=groove;TONGUE[name]=tongue;FEATURES[name]=allvoid
 return a.as_original().simplify(1e-5),b.as_original().simplify(1e-5),saddle
HA,HB,HTAB=build_case(False)
CA,CB,CTAB=build_case(True)
bs=trimesh.load(HERE.parents[1]/'vendor/miniworks/Meshtastic_Miniworks_Button.3MF');bt=list(bs.geometry.values())[0]
# The original button sits away from the origin in its file: centre it on its own bounding box first.
BUTTON=M.Manifold(M.Mesh(np.asarray(bt.vertices-bt.bounds.mean(axis=0),np.float32),np.asarray(bt.faces,np.uint32)))
FLIP=np.array([[1,0,0,0],[0,-1,0,0],[0,0,-1,0]],float)
BUTTON_R=np.array([[0,1,0,0],[0,0,1,0],[1,0,0,0]],float)
def place(m,R=None):
 if R is not None:m=m.transform(R)
 z=m.bounding_box();return m.translate((-z[0],-z[1],-z[2]))
assembled={'handheld_back':HA,'handheld_front':HB,'camera_back':CA,'camera_front':CB}
parts={n:place(m,FLIP if n.endswith('front')else None)for n,m in assembled.items()};parts['button']=place(BUTTON,BUTTON_R)
STL_NAMES={n:'miniworks_integrated_'+n+'.stl'for n in parts}
def tm(m):
 q=m.as_original().simplify(1e-5).to_mesh();t=trimesh.Trimesh(q.vert_properties[:,:3],q.tri_verts,process=True)
 f=t.faces;t.update_faces((f[:,0]!=f[:,1])&(f[:,0]!=f[:,2])&(f[:,1]!=f[:,2]));t.update_faces(t.unique_faces());t.remove_unreferenced_vertices();return t
