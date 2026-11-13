"""Export the refined case: STLs and 3MF plates into print/, plus dimensions.json. The vendor originals are not modified."""
from pathlib import Path
from xml.etree.ElementTree import Element,SubElement,tostring
import json,zipfile,numpy as np,trimesh
import design as b
HERE=Path(__file__).resolve().parent
OUT=HERE/'print';OUT.mkdir(exist_ok=True)

def exported_mesh(m):
 t=b.tm(m)
 if not(t.is_watertight and t.is_winding_consistent and t.body_count==1):raise RuntimeError('Invalid mesh export')
 return t
for name,m in b.parts.items():
 t=exported_mesh(m);t.export(OUT/b.STL_NAMES[name])
 print(name,np.round(t.extents,3).tolist(),flush=True)

def plate(name,items):
 ns='http://schemas.microsoft.com/3dmanufacturing/core/2015/02'
 root=Element('model',{'xmlns':ns,'unit':'millimeter','xml:lang':'en-US'})
 SubElement(root,'metadata',{'name':'Title'}).text=name
 SubElement(root,'metadata',{'name':'Designer'}).text='Aaron Phull (SLR Remote), adapted from Meshtastic Heltec V4 Miniworks case by Chem_Kid'
 SubElement(root,'metadata',{'name':'LicenseTerms'}).text='CC BY-NC 4.0; adapted from Thingiverse thing 7367243'
 resources=SubElement(root,'resources');build=SubElement(root,'build');settings=Element('config')
 for i,(label,part,x,y) in enumerate(items,1):
  t=exported_mesh(b.parts[part]);t.apply_translation((x,y,0))
  if not(t.bounds[0,0]>=21 and t.bounds[0,1]>=29 and t.bounds[1,0]<=250 and t.bounds[1,1]<=250):raise RuntimeError('Plate placement outside X1C safe area')
  obj=SubElement(resources,'object',{'id':str(i),'type':'model','name':label});mesh=SubElement(obj,'mesh');vertices=SubElement(mesh,'vertices');triangles=SubElement(mesh,'triangles')
  for v in t.vertices:SubElement(vertices,'vertex',dict(zip(('x','y','z'),(format(float(q),'.12g')for q in v))))
  for f in t.faces:SubElement(triangles,'triangle',dict(zip(('v1','v2','v3'),(str(int(q))for q in f))))
  SubElement(build,'item',{'objectid':str(i)})
  conf=SubElement(settings,'object',{'id':str(i)})
  vals={'name':label,'extruder':'1','enable_support':'0','brim_type':'no_brim'}
  if part in ['action_button','reset_RST']:vals.update(brim_type='outer_only',brim_width='3')
  for k,v in vals.items():SubElement(conf,'metadata',{'key':k,'value':v})
  sub=SubElement(conf,'part',{'id':str(i),'subtype':'normal_part'});SubElement(sub,'metadata',{'key':'name','value':label})
 with zipfile.ZipFile(OUT/name,'w',zipfile.ZIP_DEFLATED)as z:
  z.writestr('3D/3dmodel.model',tostring(root,encoding='utf-8',xml_declaration=True))
  z.writestr('Metadata/model_settings.config',tostring(settings,encoding='utf-8',xml_declaration=True))
  z.writestr('_rels/.rels','<?xml version="1.0"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
  z.writestr('[Content_Types].xml','<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"><Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/><Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
plate('handheld_refined.3mf',[('handheld_back','handheld_back',40,60),('handheld_front','handheld_front',100,60)])
plate('camera_refined_flat_back.3mf',[('camera_back_flat','camera_back',40,60),('camera_front','camera_front',100,60)])
plate('buttons_refined.3mf',[(n,'action_button'if n.endswith('PRG')else'reset_RST',45+i*20,60)for i,n in enumerate(['handheld_PRG','handheld_RST','camera_PRG','camera_RST'])])
# Tops-only reprint: both fronts and two shoot plungers (the printed bottoms and reset buttons are reused).
plate('tops_and_shoot_buttons.3mf',[('handheld_front','handheld_front',40,60),('camera_front','camera_front',100,60),('handheld_PRG','action_button',160,60),('camera_PRG','action_button',180,60)])
(HERE/'dimensions.json').write_text(json.dumps({n:{'print_extents_mm':list(m.bounding_box()[i+3]-m.bounding_box()[i]for i in range(3))}for n,m in b.parts.items()},indent=2))
