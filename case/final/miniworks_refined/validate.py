"""Final actual-export checks: fit, corrected roles, USB joint, seams and layers."""
from pathlib import Path
import json,hashlib,zipfile,numpy as np,trimesh,manifold3d as M
from xml.etree import ElementTree as ET
import design as b
P=Path(__file__).resolve().parent;results=[];layers={};actual={}
def ck(ok,label,detail=None):
 results.append(dict(ok=bool(ok),check=label,detail=detail));print(('PASS 'if ok else'FAIL ')+label+(' '+str(detail)if detail is not None else''),flush=True)
def rec(r):return {'z':float(r[0]),'kind':r[1],'span':float(r[2]),'area':float(r[3]),'bounds':list(r[4])}
def diff(a,c):return abs((a-c).volume())+abs((c-a).volume())
for n,p in b.parts.items():
 t=trimesh.load(P/'print'/b.STL_NAMES[n],process=True);m=M.Manifold(M.Mesh(np.asarray(t.vertices,np.float32),np.asarray(t.faces,np.uint32)))
 ck(t.is_watertight and t.is_winding_consistent and t.body_count==1 and m.status()==M.Error.NoError,n+': closed single-body exported STL')
 ck(abs(t.volume-p.volume())<.015 and m.genus()==p.genus(),n+': intended topology and exported volume')
 ck(abs(t.bounds[0,2])<1e-5,n+': flat on supplied print face')
 br,fa=b.shell.ref.cb.layer_scan(m,max_span=2.4,tol=.025);layers[n]={'bridges':[rec(r)for r in br],'failures':[rec(r)for r in fa]}
 ck(not br and not fa,n+': support-free 0.20 mm layer scan',{'bridges':len(br),'failures':len(fa)})
 actual[n]=b.unpose(m,n)
act,rst=actual['action_button'],actual['reset_RST']
ck(len(act.decompose())==1 and act.genus()==0,'action cap, retaining shoulders and actuator remain one printed solid')
lower=b.box(-15,15,-15,15,0,b.CAP_TOP-b.CH-.01)
ck(diff(act^lower,b.base.ACTION^lower)<.001,'action plunger actuator, guides and retaining shoulders unchanged')
ck(abs(act.bounding_box()[5]-b.CAP_TOP)<1e-5,'action plunger height unchanged')
ck(diff(rst,b.base.RESET)<.001,'recessed reset unchanged')
ck(float(b.ACTION_CENTRE[0])<0 and float(b.RESET_CENTRE[0])>0,'photo orientation retained: PRG above USB and RST below with OLED right')
USB_EDIT=b.box(-9,9,-.2,4.1,b.S-.11,20.5)
EDIT=USB_EDIT+b.ACTION_EDIT
for device,old_a,old_f in [('handheld',b.lanyard.BACK,b.base.HB),('camera',b.base.CA,b.base.CB)]:
 a,f=actual[device+'_back'],actual[device+'_front'];whole=a+f
 ck((a^f).volume()<1e-4,device+': halves mate without collision',float((a^f).volume()))
 ck(diff(a-USB_EDIT,old_a-USB_EDIT)<.003,device+': back unchanged outside USB refinement')
 ck(diff(f-EDIT,old_f-EDIT)<.003,device+': front unchanged outside USB and action edge')
 joint=b.box(-30,30,-1,85,b.S-.05,b.S+.05)-USB_EDIT
 ck(diff((a+f)^joint,(old_a+old_f)^joint)<.003,device+': existing perimeter joint retained; no added V seam')
 dims=np.array(whole.bounding_box()[3:])-np.array(whole.bounding_box()[:3])
 ck(np.allclose(dims,[45.72,81.28 if device=='camera'else 60.96,b.H],atol=1e-4),device+': outside body dimensions retained',dims.tolist())
 for label,void in [('padded 51.5 x 35 x 6.6 battery',b.shell.cell),('inferred PCB plate',b.shell.board),('original screen opening/recess',b.shell.WINDOW_VOID),('USB metal/receptacle opening',b.shell.USB_PORT),('rounded USB plug clearance',b.USB_RECESS),('original USB plug clearance',b.shell.USB_RECESS),('SMA passage',b.shell.SMA_PASS),('SMA nut',b.shell.SMA_NUT),('SMA barrel',b.shell.SMA_BARREL)]:
  ck(abs((whole^void).volume())<.001,device+': '+label+' clear')
 rr=b.box(float(b.RESET_CENTRE[0])-3.9,float(b.RESET_CENTRE[0])+3.9,1.6,9.7,16.4,b.H+.1)
 ck(diff(f^rr,old_f^rr)<.001,device+': reset access and recess retained')
 apertures={}
 for poly in f.slice(b.H-.1).to_polygons():
  q=np.asarray(poly);c=(q.min(0)+q.max(0))/2;area=.5*np.sum(q[:,0]*np.roll(q[:,1],-1)-q[:,1]*np.roll(q[:,0],-1))
  if area<0 and abs(float(c[1])-float(b.base.CY))<.5 and abs(abs(float(c[0]))-8.25)<.2:apertures['above'if c[0]<0 else'below']=float(-area)
 ck(set(apertures)=={'above','below'}and act.bounding_box()[5]-rst.bounding_box()[5]>1.5,device+': PRG above USB and RST below, both round; shoot plunger stands taller than the recessed reset')
 for name,p,centre,zbase in [('PRG',act,b.ACTION_CENTRE,0),('RST',rst,b.RESET_CENTRE,b.base.REST_Z)]:
  def moved(d):return p.translate((centre[0],centre[1],zbase-float(d)))
  vals=[abs((moved(d)^whole).volume())for d in np.linspace(0,1.2,25)]
  ck(max(vals)<1e-4,device+': '+name+' clears both halves through 1.2 mm press',float(max(vals)))
  install=[abs((moved(d)^f).volume())for d in np.linspace(0,14,57)]
  ck(max(install)<1e-4,device+': '+name+' installs straight from inside',float(max(install)))
  ck((moved(-.65)^f).volume()>.01,device+': '+name+' remains captive outwardly')
 for sx in [-1,1]:
  for y in b.shell.SCREW_Y:
   x=sx*b.shell.SCREW_X;hits=a.ray_cast((x,y,b.S+.01),(x,y,-5));tip=b.H-b.shell.HEAD_DEPTH-16
   ck(bool(hits)and abs(hits[0].position[2]-b.shell.PILOT_BOTTOM)<1e-4,device+f': M2 x 16 pilot unchanged at ({x},{y})')
   head=b.cyl(x,y,b.H-b.shell.HEAD_DEPTH,b.H-.2,1.9)
   ck((head^whole).volume()<1e-4 and tip-b.shell.PILOT_BOTTOM>.6,device+f': M2 x 16 head/tip clearance at ({x},{y})')
 if device=='camera':
  ck((whole^b.shell.GPS_VOID).volume()<.001,'camera: original GPS cavity retained')
  plane=b.box(-18,18,8,48,0,.1)
  ck((plane-a).volume()<.001,'camera: plain flat back retained')
 else:
  ck((whole^b.lanyard.CHANNEL).volume()<.001,'handheld: complete inset USB-end lanyard route retained')
  ck(a.genus()==1,'handheld: single intended cord passage, no extra holes')
for name,count in [('handheld_refined.3mf',2),('camera_refined_flat_back.3mf',2),('buttons_refined.3mf',4)]:
 file=P/'print'/name;sc=trimesh.load(file,force='scene');ck(len(sc.geometry)==count,name+': expected object count')
 for t in sc.dump():ck(t.is_watertight and t.body_count==1 and abs(t.bounds[0,2])<1e-5,name+': closed object in supplied bed pose')
 with zipfile.ZipFile(file)as z:
  root=ET.fromstring(z.read('Metadata/model_settings.config'));vals=[q.attrib['value']for q in root.findall("./object/metadata[@key='enable_support']")]
  ck(len(vals)==count and set(vals)=={'0'},name+': supports explicitly OFF')
report={'passed':all(q['ok']for q in results),'checks':results,'print_layers':layers,'sources_sha256':{n:hashlib.sha256((P/n).read_bytes()).hexdigest()for n in ['design.py','build.py','validate.py']},'exports_sha256':{f.name:hashlib.sha256(f.read_bytes()).hexdigest()for f in (P/'print').iterdir()if f.is_file()}}
(P/'validation.json').write_text(json.dumps(report,indent=2));print(f"{sum(q['ok']for q in results)}/{len(results)} checks passed",flush=True)
if not report['passed']:raise SystemExit(1)
