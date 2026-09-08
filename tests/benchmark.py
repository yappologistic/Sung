#!/usr/bin/env python3
"""Isolated, synthetic benchmarks. Software rendering is invisible; native probes use an inactive Hyprland 0.56+ workspace without requesting focus."""
import argparse, json, os, pathlib, shlex, signal, subprocess, time
from xml.sax.saxutils import escape
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('binary',type=pathlib.Path)
p.add_argument('--output',type=pathlib.Path,required=True)
p.add_argument('--modes',default='idle,playback,lyrics,immersive,mini,hidden')
p.add_argument('--seconds',type=int,default=10)
p.add_argument('--repeats',type=int,default=3)
p.add_argument('--native-workspace',type=int,help='Inactive workspace for native allocation measurements; occluded FPS is not a rendering benchmark')
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);out.chmod(0o700)
if a.seconds<1 or a.repeats<1:p.error('seconds and repeats must be positive')
if a.native_workspace:
 active=json.loads(subprocess.check_output(['hyprctl','activeworkspace','-j']))['id']
 if active==a.native_workspace:p.error('Choose an inactive workspace')
 workspaces=json.loads(subprocess.check_output(['hyprctl','workspaces','-j']))
 if any(w['id']==a.native_workspace and w['windows'] for w in workspaces):p.error('Choose an empty workspace')
 version=json.loads(subprocess.check_output(['hyprctl','version','-j']))['version']
 if '0.56' not in version:p.error('Native launch currently supports Hyprland 0.56; use invisible software measurements on other versions')
audio=out/'silence.wav'
subprocess.run(['ffmpeg','-v','error','-f','lavfi','-i','anullsrc=r=48000:cl=stereo','-t','600','-c:a','pcm_s16le',str(audio)],check=True)
tracks=[dict(id=f'{i:011d}',videoId=f'{i:011d}',kind='song',title=f'Benchmark track {i}',artist='Sung performance fixture',seconds=600,art=f'https://sung-benchmark.invalid/{i%80}.png') for i in range(200)]
helper=out/'fixture.py'
helper.write_text('import json,sys\nr=json.load(sys.stdin)\ntracks='+repr(tracks)+'\nprint(json.dumps({"ok":True,"sections":[{"title":"Benchmark shelf","items":tracks[:20]}],"lyrics":"Benchmark lyrics","lines":[{"start":i*4000,"end":(i+1)*4000,"text":"A quiet line across the evening sky" if i%2 else "The music carries on"} for i in range(150)]}))\n')
font=subprocess.check_output(['fc-match','Google Sans Flex','-f','%{file}'],text=True).strip()
fonts=out/'fonts.conf';fonts.write_text('<?xml version="1.0"?><!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd"><fontconfig><include>/etc/fonts/fonts.conf</include><dir>'+escape(str(pathlib.Path(font).parent))+'</dir></fontconfig>')
results=[]
for repeat in range(a.repeats):
 for mode in a.modes.split(','):
  if mode not in ('idle','playback','lyrics','immersive','mini','hidden'):p.error('Unknown mode: '+mode)
  run=out/f'{repeat}-{mode}';run.mkdir();env=os.environ.copy()
  for key,folder in [('XDG_DATA_HOME','data'),('XDG_CONFIG_HOME','config'),('XDG_CACHE_HOME','cache')]:env[key]=str(run/folder)
  library=run/'data/Sung/sung';library.mkdir(parents=True)
  (library/'library.json').write_text(json.dumps(dict(queue=tracks,index=0,position=60100)))
  env.pop('QT_QUICK_BACKEND',None)
  if not a.native_workspace:env['QSG_RENDER_LOOP']='basic'
  env.update(QT_QPA_PLATFORM='wayland' if a.native_workspace else 'offscreen',QT_QPA_PLATFORMTHEME='generic',FONTCONFIG_FILE=str(fonts),SUNG_HELPER=str(helper),SUNG_PYTHON='python3',SUNG_BENCH_AUDIO=str(audio),SUNG_BENCH_MODE=mode,SUNG_BENCH_MS=str(a.seconds*1000),SUNG_BENCH_OUTPUT=str(run),SUNG_BENCH_ART='1')
  if not a.native_workspace:env['QT_QUICK_BACKEND']='software'
  subprocess.run([str(a.binary.resolve().with_name('sung-artwork-fixture'))],env=env,check=True)
  env['SUNG_BENCH_ART_PRESEEDED']='1'
  command=[str(a.binary.resolve()),'--isolated','--benchmark']
  (run/'ready').touch()
  if a.native_workspace:
   # exec preserves the launch PID so compositor rules apply to the test app.
   launcher=run/'launch.py';launcher.write_text('import os\nos.environ.update('+repr(env)+')\nopen('+repr(str(run/'pid'))+',"w").write(str(os.getpid()))\nf=os.open('+repr(str(run/'private.log'))+',os.O_WRONLY|os.O_CREAT|os.O_TRUNC,0o600)\nos.dup2(f,1);os.dup2(f,2)\nos.execv('+repr(command[0])+','+repr(command)+')\n')
   lua='hl.exec_cmd('+json.dumps(shlex.join(['python3',str(launcher)]))+', {workspace='+json.dumps(str(a.native_workspace)+' silent')+',float=true,size="1180 800",no_initial_focus=true,suppress_event="activate activatefocus"})'
   subprocess.run(['hyprctl','eval',lua],check=True,capture_output=True)
   deadline=time.monotonic()+a.seconds+45
   result_file=run/(mode+'.json')
   while time.monotonic()<deadline:
    if result_file.exists() and result_file.stat().st_size:break
    time.sleep(.1)
   else:
    # Only terminate the specific test executable launched by this run.
    if (run/'pid').exists():
     pid=int((run/'pid').read_text())
     try:
      if pathlib.Path(f'/proc/{pid}/exe').resolve()==a.binary.resolve():os.kill(pid,signal.SIGTERM)
     except (OSError,ValueError):pass
    raise RuntimeError('Native benchmark timed out; inspect its private.log')
   time.sleep(1) # allow final capture and clean exit before the next run
  else:
   with (run/'private.log').open('w') as log:
    subprocess.run(command,env=env,stdout=log,stderr=log,timeout=a.seconds+45,check=True)
  result=json.loads((run/(mode+'.json')).read_text());result['repeat']=repeat
  result['rendering_context']='native background allocation probe' if a.native_workspace else 'software fixture; not native GPU performance'
  results.append(result);(out/'results.json').write_text(json.dumps(results,indent=2)+'\n')
  print(json.dumps(result),flush=True)
