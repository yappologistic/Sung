#!/usr/bin/env python3
"""Repeatable verification. All profiles, logs, and captures stay in the report folder."""
import argparse, datetime, json, os, pathlib, re, shutil, subprocess, sys, time
from xml.sax.saxutils import escape
root=pathlib.Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--offline',action='store_true',help='Skip network-dependent playback checks; report them as skipped')
p.add_argument('--output',type=pathlib.Path,help='Report folder (created with private permissions)')
p.add_argument('--jobs',type=int,default=4)
p.add_argument('--build-dir',type=pathlib.Path,help='Reuse a diagnostic build directory')
a=p.parse_args()
out=(a.output or root/'verification'/datetime.datetime.now().strftime('%Y%m%d-%H%M%S')).resolve()
out.mkdir(parents=True,exist_ok=False);out.chmod(0o700)
rows=[]
def stage(name,cmd,timeout=600,env=None):
    print(f'▶ {name}',flush=True)
    start=time.monotonic()
    target=out/(name+'.log')
    try:
        with target.open('w') as log:
            r=subprocess.run(cmd,cwd=root,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=timeout)
        text=target.read_text(errors='replace')
        problems=re.findall(r'(?:ReferenceError|TypeError|Binding loop|Unable to assign|Cannot assign|FAIL)[^\n]*',text)
        good=r.returncode==0 and not problems
        why='; '.join(problems[:3]) if problems else f'exit {r.returncode}'
    except (subprocess.TimeoutExpired,OSError) as e:
        good=False;why=str(e)
    rows.append(dict(stage=name,status='pass' if good else 'fail',seconds=round(time.monotonic()-start,2),detail=why,log=target.name))
    print(f'  {"PASS" if good else "FAIL"} {name} ({rows[-1]["seconds"]}s)',flush=True)
    return good
build=a.build_dir.resolve() if a.build_dir else out/'build'
env=os.environ.copy()
# Do not inherit a desktop-forced threaded render loop into the software harness.
# Qt Quick Shapes can race window teardown there; native GPU tests keep their own loop.
env.update(QT_FORCE_STDERR_LOGGING='1',QT_QPA_PLATFORMTHEME='generic',QT_QPA_PLATFORM='offscreen',QT_QUICK_BACKEND='software',QSG_RENDER_LOOP='basic',QT_FFMPEG_DECODING_HW_DEVICE_TYPES=',',QT_FFMPEG_ENCODING_HW_DEVICE_TYPES=',')
# Isolated XDG_DATA_HOME also hides user-installed fonts from fontconfig.
# Expose only the requested font directory, not the user's application data.
if shutil.which('fc-match'):
    font_file=subprocess.check_output(['fc-match','Google Sans Flex','-f','%{file}'],text=True).strip()
    font_config=out/'fonts.conf'
    font_config.write_text('<?xml version="1.0"?><!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd"><fontconfig><include>/etc/fonts/fonts.conf</include><dir>'+escape(str(pathlib.Path(font_file).parent))+'</dir></fontconfig>')
    env['FONTCONFIG_FILE']=str(font_config)

def profile(name):
    e=env.copy();base=out/name
    for var,folder in [('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_CACHE_HOME','cache')]:e[var]=str(base/folder)
    return e
ready=stage('configure',['cmake','-S',str(root),'-B',str(build),'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=ON','-DSUNG_DIAGNOSTICS=ON'])
if ready: ready=stage('build',['cmake','--build',str(build),'--parallel',str(max(1,a.jobs))])
if ready:
    stage('backend',['ctest','--test-dir',str(build),'--output-on-failure'],120,profile('unit-profile'))
    stage('catalog',['python3','-m','unittest','discover','-s',str(root/'tests'),'-p','test_*.py'],60,env)
    if shutil.which('dbus-run-session') and shutil.which('qdbus6'):
        stage('mpris',['dbus-run-session','--','python3',str(root/'tests/mpris_test.py'),str(build/'sung')],40,profile('mpris-profile'))
    else: rows.append(dict(stage='mpris',status='fail',detail='dbus-run-session and qdbus6 are required'))
    e=profile("folder-import-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON="/usr/bin/python3",SUNG_TEST_OUTPUT=str(out/"folder-import"))
    stage("folder-import",[str(build/"sung"),"--isolated","--folder-import-test"],60,e)
    e=profile('search-selection-profile');e.update(SUNG_HELPER=str(root/'tests/catalog_fixture.py'),SUNG_PYTHON='/usr/bin/python3',SUNG_TEST_OUTPUT=str(out/'search-selection'))
    stage('search-selection',[str(build/'sung'),'--isolated','--search-selection-test'],120,e)
    e=profile("qol-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON="/usr/bin/python3",SUNG_TEST_OUTPUT=str(out/"qol"))
    stage("qol",[str(build/"sung"),"--isolated","--qol-test"],60,e)
    e=profile("interaction-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON="/usr/bin/python3",SUNG_TEST_OUTPUT=str(out/"interaction"))
    stage("interaction",[str(build/"sung"),"--isolated","--interaction-test"],90,e)
    e=profile("audio-indicator-profile");e.update(SUNG_TEST_OUTPUT=str(out/"audio-indicator"))
    stage("audio-indicator",[str(build/"sung"),"--isolated","--audio-indicator-test"],40,e)
    e=profile("visual-delight-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON="/usr/bin/python3",SUNG_TEST_OUTPUT=str(out/"visual-delight"))
    stage("visual-delight",[str(build/"sung"),"--isolated","--visual-delight-test"],90,e)
    e=profile("library-qol-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON="/usr/bin/python3",SUNG_TEST_OUTPUT=str(out/"library-qol"))
    stage("library-qol",[str(build/"sung"),"--isolated","--library-qol-test"],60,e)
    e=profile("visual-polish-profile");e.update(SUNG_HELPER=str(root/"tests/catalog_fixture.py"),SUNG_PYTHON="/usr/bin/python3",SUNG_TEST_OUTPUT=str(out/"visual-polish"))
    stage("visual-polish",[str(build/"sung"),"--isolated","--visual-polish-test"],40,e)
    if a.offline:
        rows.append(dict(stage='live-ui-and-audit',status='skipped',detail='--offline selected; streaming and live catalog not verified'))
    else:
        py=os.environ.get('SUNG_PYTHON',str(root/'runtime/bin/python'))
        if not pathlib.Path(py).exists():
            rows.append(dict(stage='live-runtime',status='fail',detail='Run scripts/setup.sh or set SUNG_PYTHON'))
        else:
            for name,flag in [('native-features','--features-test'),('live-lyrics-motion','--lyrics-test'),('playback-recovery','--recovery-test'),('ui-playback','--ui-test'),('ui-audit','--audit')]:
                e=profile(name+'-profile');e.update(SUNG_PYTHON=py,SUNG_HELPER=str(root/'helper/catalog.py'),SUNG_TEST_OUTPUT=str(out/name))
                stage(name,[str(build/'sung'),'--isolated',flag],480,e)
            e=profile('hidpi-profile');e.update(SUNG_PYTHON=py,SUNG_HELPER=str(root/'helper/catalog.py'),SUNG_TEST_OUTPUT=str(out/'hidpi'),QT_SCALE_FACTOR='1.6')
            stage('ui-hidpi',[str(build/'sung'),'--isolated','--ui-test'],480,e)
    stage('idle-performance',['python3',str(root/'tests/profile.py'),str(build/'sung')],20,profile('performance-profile'))
    stage('mini-performance',['python3',str(root/'tests/profile.py'),str(build/'sung'),'--mini'],20,profile('mini-performance-profile'))
    stage('noctalia-preview',[str(build/'sung'),'--isolated','--offline','--screenshot',str(out/'noctalia.png')],15,{**profile('theme-profile'),'SUNG_NOCTALIA_COLORS':os.environ.get('SUNG_NOCTALIA_COLORS',str(pathlib.Path.home()/'.local/share/color-schemes/noctalia.colors'))})
summary={'created':datetime.datetime.now().astimezone().isoformat(),'offline':a.offline,'passed':all(r['status']!='fail' for r in rows),'stages':rows,'diagnostic_binary_bytes':(build/'sung').stat().st_size if ready else None,'note':'Screenshots require human visual review. Live checks depend on YouTube and the network. Diagnostic binary includes QtTest; measure the Release executable separately.'}
(out/'report.json').write_text(json.dumps(summary,indent=2)+'\n')
(out/'report.txt').write_text('\n'.join(f"{r['status'].upper():7} {r['stage']}: {r.get('detail','')}" for r in rows)+'\n')
print(f'Report: {out}/report.json',flush=True)
sys.exit(0 if summary['passed'] else 1)
