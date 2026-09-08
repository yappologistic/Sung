#!/usr/bin/env python3
"""Compare diagnostic builds through their first software-rendered frame, using fresh profiles."""
import argparse, json, os, pathlib, re, subprocess
from xml.sax.saxutils import escape
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('before',type=pathlib.Path);p.add_argument('after',type=pathlib.Path)
p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--repeats',type=int,default=12)
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);out.chmod(0o700)
font=subprocess.check_output(['fc-match','Google Sans Flex','-f','%{file}'],text=True).strip()
fonts=out/'fonts.conf';fonts.write_text('<?xml version="1.0"?><!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd"><fontconfig><include>/etc/fonts/fonts.conf</include><dir>'+escape(str(pathlib.Path(font).parent))+'</dir></fontconfig>')
results=[]
for i in range(a.repeats):
 for name in (('before','after') if i%2==0 else ('after','before')):
  run=out/f'{i}-{name}';run.mkdir();env=os.environ.copy()
  env.update(QT_QPA_PLATFORM='offscreen',QT_QPA_PLATFORMTHEME='generic',QT_QUICK_BACKEND='software',QSG_RENDER_LOOP='basic',SUNG_STARTUP_PROBE='1',FONTCONFIG_FILE=str(fonts))
  for key,folder in [('XDG_DATA_HOME','data'),('XDG_CONFIG_HOME','config'),('XDG_CACHE_HOME','cache')]:env[key]=str(run/folder)
  r=subprocess.run([str(getattr(a,name).resolve()),'--isolated','--offline'],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=15,check=True)
  (run/'run.log').write_text(r.stdout)
  values={key.lower()+'_ms':float(value) for key,value in re.findall(r'STARTUP_(READY|FRAME)_MS ([\d.]+)',r.stdout)}
  if len(values)!=2:raise RuntimeError('Both builds must enable SUNG_DIAGNOSTICS with the startup probe')
  results.append(dict(build=name,repeat=i,**values));(out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps(results,indent=2))
