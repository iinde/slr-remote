"""Accurate final-CAD views with illustrative electronics and purchased antennas.

All printable geometry is read from final exported STLs. Antennas, display and
screw heads are illustrative and are never included in printable files.
"""
from pathlib import Path
import sys,numpy as np,trimesh,manifold3d as M,pyvista as pv
from PIL import Image,ImageDraw,ImageFont
import design as b
P=Path(__file__).resolve().parent
# The output folder is only taken from the command line (and created) when run as a script,
# so importing this module from another tool has no side effects on disk.
OUT=Path(sys.argv[1]).resolve()if __name__=='__main__'and len(sys.argv)>1 else P/'reference_views'
sys.path.insert(0,str(P.parents[1]/'concepts'));from render_kit import _pv
pv.OFF_SCREEN=True
font='/System/Library/Fonts/Helvetica.ttc'
def actual(n):
 t=trimesh.load(P/'print'/b.STL_NAMES[n]);m=M.Manifold(M.Mesh(np.asarray(t.vertices,np.float32),np.asarray(t.faces,np.uint32)))
 return b.unpose(m,n)
ACTUAL={n:actual(n)for n in b.parts}
def y_cyl(y0,y1,r,r1=None):return M.Manifold.cylinder(y1-y0,r,r if r1 is None else r1,128).rotate((-90,0,0)).translate((-11.43,y0,b.S))
def x_cyl(x0,x1,y,z,r):return M.Manifold.cylinder(x1-x0,r,r,96).rotate((0,90,0)).translate((x0,y,z))
def antennas(camera):
 metal=y_cyl(60.7,63.3,3.05)+y_cyl(61.1,61.9,3.8)
 if not camera:
  black=y_cyl(63.1,69.0,4.5)+y_cyl(68.9,84.0,4.2)+y_cyl(83.9,89.0,4.2,3.5)+M.Manifold.sphere(3.5,64).translate((-11.43,89,b.S))
  for y in [64.3,65.7,67.1]:black=black+y_cyl(y,y+.35,4.65)
  return [(metal,'#a6a8aa'),(black,'#25292c')]
 y=67.3
 black=y_cyl(63.1,y,4.4)+M.Manifold.sphere(4.2,64).translate((-11.43,y,b.S))
 black=black+b.cyl(-11.43,y,b.S,b.S+9.0,4.2)+b.cyl(-11.43,y,b.S+8.9,b.S+26,4.0)+b.cyl(-11.43,y,b.S+25.9,b.S+30,4.0,3.3)+M.Manifold.sphere(3.3,64).translate((-11.43,y,b.S+30))
 metal=metal+x_cyl(-15.85,-15.65,y,b.S,2.0)+x_cyl(-7.21,-7.01,y,b.S,2.0)
 return [(metal,'#a6a8aa'),(black,'#25292c')]
HEADS=[]
for sx in [-1,1]:
 for y in b.shell.SCREW_Y:
  x=sx*b.shell.SCREW_X;z=b.H-b.shell.HEAD_DEPTH
  h=b.cyl(x,y,z,z+2,1.85)-M.CrossSection.circle(.78,6).extrude(.8).translate((x,y,z+1.25))
  HEADS.append(h)
PALETTES={
 'graphite_orange':('#343a3f','#343a3f','#d56d35','#343a3f'),
 'stone_charcoal':('#343a3f','#ded8cb','#ce6b36','#454c50'),
 'petrol_slate':('#4b545d','#47717f','#d4d8d8','#3c5965')}
def scene(device,palette):
 bc,fc,ac,rc=PALETTES[palette]
 items=[(ACTUAL[device+'_back'],bc),(ACTUAL[device+'_front'],fc),(ACTUAL['action_button'].translate((b.base.CX,b.base.CY,0)),ac),(ACTUAL['reset_RST'].translate((b.RESET_CENTRE[0],b.RESET_CENTRE[1],b.base.REST_Z)),rc)]
 items.extend((q,'#25292c')for q in HEADS)
 items.extend(antennas(device=='camera'))
 usb_outer=shell_usb=shell_usb_inner=None
 usb_outer=b.shell.xz_shape(b.shell.rrect(-4.4,4.4,15.12,17.94,1.0),1.55,2.5)
 usb_inner=b.shell.xz_shape(b.shell.rrect(-3.65,3.65,15.62,17.44,.6),1.5,2.6)
 items.extend([(usb_outer-usb_inner,'#a5a9ad'),(b.box(-4.3,4.3,2.51,2.54,15.2,17.9),'#11171b'),(b.box(-2.8,2.8,1.7,2.55,16.35,16.6),'#353a3f')])
 return items
def view(items,pos,focus,up,scale,size=(1400,1050),display=True):
 p=pv.Plotter(off_screen=True,window_size=size,lighting='three lights');p.set_background('#eceeef')
 for m,c in items:p.add_mesh(_pv(m),color=c,smooth_shading=False,ambient=.28,specular=.15,specular_power=20)
 if display:
  # This plane illustrates the installed OLED; it is not a fit-checked part.
  plane=pv.Plane(center=(0,0,0),direction=(0,0,1),i_size=35,j_size=20,i_resolution=1,j_resolution=1)
  plane.rotate_z(90,inplace=True);plane.translate((0,33.5,20.2),inplace=True)
  pic=Image.new('RGB',(512,256),'#0a1318');d=ImageDraw.Draw(pic)
  d.text((24,14),'SHOT',font=ImageFont.truetype(font,28),fill='#77ccd8')
  d.text((24,83),'READY',font=ImageFont.truetype(font,64),fill='#b8e8e9')
  d.rectangle((20,199,491,243),fill='#36616b');d.text((30,204),'HOLD: FOCUS',font=ImageFont.truetype(font,24),fill='#b8e8e9')
  p.add_mesh(plane,texture=pv.numpy_to_texture(np.asarray(pic)),ambient=1,diffuse=0,specular=0)
 p.enable_parallel_projection();p.camera.position=pos;p.camera.focal_point=focus;p.camera.up=up;p.camera.parallel_scale=scale;p.enable_anti_aliasing('ssaa')
 a=p.screenshot(return_img=True);p.close();return Image.fromarray(a[:,:,:3])

if __name__=='__main__':
 OUT.mkdir(parents=True,exist_ok=True)
 # Two views show the USB surround before and after the refinement.
 old=[(b.lanyard.BACK,'#8c9eac'),(b.base.HB,'#d0ccc3')]
 new=[(ACTUAL['handheld_back'],'#8c9eac'),(ACTUAL['handheld_front'],'#d0ccc3')]
 cmp=Image.new('RGB',(2400,1020),'white');draw=ImageDraw.Draw(cmp)
 for i,(items,title)in enumerate([(old,'Previous USB surround'),(new,'Refined rounded flare and tapered joint')]):
  draw.text((1200*i+25,24),title,font=ImageFont.truetype(font,32),fill='#23313d')
  cmp.paste(view(items,(0,-65,32),(0,1,14),(0,0,1),10,size=(1200,900),display=False),(1200*i,110))
 cmp.save(OUT/'usb_comparison.png')
 for device in ['handheld','camera']:
  focus=(0,44,16 if device=='handheld'else 20);scale=58 if device=='handheld'else 62
  poses=[((105,-125,135),(0,0,1)),((-95,145,115),(0,0,1)),((95,135,-130),(0,1,0)),((140,-65,48),(0,0,1))]
  for palette in PALETTES:
   sheet=Image.new('RGB',(2800,2100),'#eceeef')
   for i,(pos,up)in enumerate(poses):
    im=view(scene(device,palette),pos,focus,up,scale)
    im.save(OUT/(device+'_'+palette+'_view'+str(i+1)+'.png'))
    sheet.paste(im,(i%2*1400,i//2*1050))
   sheet.save(OUT/(device+'_'+palette+'_reference.png'))
 print('Reference views saved to',OUT,flush=True)
