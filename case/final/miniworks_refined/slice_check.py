"""Verify both exported plates in the installed Bambu Studio CLI; never prints."""
from pathlib import Path
import json,subprocess,re
P=Path(__file__).resolve().parent
APP=Path('/Applications/BambuStudio.app/Contents')
PROFILES=APP/'Resources/profiles/BBL'
OUT=P/'slicer_check';OUT.mkdir(exist_ok=True)
PD=OUT/'profiles';PD.mkdir(exist_ok=True)
def flatten(rel,extra=None):
 file=PROFILES/rel;chain=[]
 for i in range(12):
  d=json.loads(file.read_text());chain.append(d)
  if not d.get('inherits'):break
  file=file.parent/(d['inherits']+'.json')
 else:raise RuntimeError('Profile inheritance too deep')
 flat={}
 for q in reversed(chain):flat.update(q)
 flat.pop('inherits',None);flat['from']='system';flat.update(extra or {})
 file=PD/Path(rel).name;file.write_text(json.dumps(flat,indent=2));return file
mach=flatten('machine/Bambu Lab X1 Carbon 0.4 nozzle.json')
proc=flatten('process/0.20mm Standard @BBL X1C.json',{'curr_bed_type':'Textured PEI Plate','enable_support':'0','brim_type':'auto_brim','brim_width':'5'})
fil=flatten('filament/Bambu PETG Basic @BBL X1C.json')
results=[]
for name in ['handheld_refined','camera_refined_flat_back','buttons_refined','tops_and_shoot_buttons']:
 d=OUT/name;d.mkdir(exist_ok=True)
 cmd=[str(APP/'MacOS/BambuStudio'),'--arrange','0','--orient','0','--slice','0','--load-settings',f'{mach};{proc}','--load-filaments',str(fil),'--outputdir',str(d),str(P/'print'/(name+'.3mf'))]
 print('Slicing',name,flush=True)
 r=subprocess.run(cmd,capture_output=True,text=True,timeout=600)
 (d/'cli.log').write_text(r.stdout+'\n'+r.stderr)
 js=json.loads((d/'result.json').read_text()) if (d/'result.json').exists() else {}
 g=list(d.glob('*.gcode'))
 record=dict(plate=name,returncode=r.returncode,error=js.get('error_string'),gcode_count=len(g))
 if g:
  txt=g[0].read_text();header={}
  for line in txt.splitlines():
   m=re.match(r'; (\w+) = (.*)$',line)
   if m and m[1] not in header:header[m[1]]=m[2]
  record.update(filament_type=header.get('filament_type'),printable_area=header.get('printable_area'),support=header.get('enable_support'),layer_height=header.get('layer_height'),bed_type=header.get('curr_bed_type'))
  record['time']=re.findall(r'; (?:model printing time|total estimated time).*',txt[:10000])
 record['warnings']=[q.get('warning_message','') for q in js.get('sliced_plates',[]) if q.get('warning_message')]
 record['support_features']=len(re.findall(r'^; FEATURE: Support',txt,re.MULTILINE)) if g else -1
 record['passed']=record['support_features']==0 and record.get('support')=='0' and not record['warnings'] and r.returncode==0 and record['error']=='Success.' and bool(g) and record.get('filament_type')=='PETG' and record.get('printable_area')=='0x0,256x0,256x256,0x256'
 results.append(record);print(json.dumps(record),flush=True)
(OUT/'summary.json').write_text(json.dumps(results,indent=2))
if not all(r['passed'] for r in results):raise SystemExit(1)
