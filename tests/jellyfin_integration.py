#!/usr/bin/env python3
"""Test Sung against a disposable Jellyfin server and generated audio."""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import secrets
import shlex
import shutil
from xml.sax.saxutils import escape
import socket
import subprocess
import time
import urllib.error
import urllib.parse
import urllib.request


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--server-binary',type=Path,required=True)
    p.add_argument('--test-binary',type=Path)
    p.add_argument('--ui-binary',type=Path)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--native-ui',action='store_true',help='Run UI checks on Hyprland workspace 2')
    p.add_argument('--prepare-only',action='store_true',help='Leave the fixture server running for interactive development')
    args=p.parse_args()
    root=args.output.resolve();root.mkdir(parents=True,exist_ok=False);root.chmod(0o700)
    for folder in ['music','other-music','data','config','cache','logs','client-config','client-data','client-cache']:(root/folder).mkdir()
    def encode(i):
        album=root/'music'/f'Album {i//35:02}';album.mkdir(exist_ok=True)
        ext=['flac','mp3','opus'][i%3];song=album/f'{i:03}.{ext}'
        subprocess.run(['ffmpeg','-nostdin','-v','error','-f','lavfi','-i','sine=frequency=220:sample_rate=48000','-t','12','-af','volume=0.001','-metadata',f'title=Fixture {i:03}','-metadata','artist=Sung Test Artist','-metadata',f'album=Fixture Album {i//35:02}','-metadata','album_artist=Sung Test Artist','-metadata','genre=Test','-metadata',f'track={i%35+1}','-metadata','disc=1',str(song)],check=True)
        if i!=104:song.with_suffix('.lrc').write_text('[00:00.00]First test line\n[00:04.00]Second test line\n[00:08.00]Last test line\n')
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:list(pool.map(encode,range(105)))
    for album in (root/'music').iterdir():
        subprocess.run(['ffmpeg','-nostdin','-v','error','-f','lavfi','-i','color=c=0x88ccee:s=96x96','-frames:v','1',str(album/'cover.jpg')],check=True)
    (root/'other-music/Other Album').mkdir()
    subprocess.run(['ffmpeg','-nostdin','-v','error','-f','lavfi','-i','anullsrc','-t','2','-metadata','title=Other library song','-metadata','album=Other album',str(root/'other-music/Other Album/other.flac')],check=True)
    with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    base=f'http://127.0.0.1:{port}/jellyfin'
    (root/'config/network.xml').write_text(f'''<?xml version="1.0"?><NetworkConfiguration><BaseUrl>/jellyfin</BaseUrl><InternalHttpPort>{port}</InternalHttpPort><EnableIPv4>true</EnableIPv4><EnableIPv6>false</EnableIPv6><EnableHttps>false</EnableHttps><RequireHttps>false</RequireHttps><EnableRemoteAccess>false</EnableRemoteAccess><EnableUPnP>false</EnableUPnP><EnablePublishedServerUriByRequest>false</EnablePublishedServerUriByRequest><LocalNetworkAddresses><string>127.0.0.1</string></LocalNetworkAddresses></NetworkConfiguration>''')
    username='sung-fixture';password=secrets.token_hex(24);token=''
    def api(path,body=None,method=None):
        headers={'Content-Type':'application/json','Authorization':'MediaBrowser Client="SungTest", Device="Fixture", DeviceId="sung-fixture", Version="1"'+(f', Token="{token}"' if token else '')}
        req=urllib.request.Request(base+path,data=None if body is None else json.dumps(body).encode(),headers=headers,method=method)
        with urllib.request.urlopen(req,timeout=15) as res:
            data=res.read();return json.loads(data) if data else {}
    with (root/'server.log').open('w') as log:
        server=subprocess.Popen([str(args.server_binary.resolve()),'--nowebclient','--nonetchange','--datadir',str(root/'data'),'--configdir',str(root/'config'),'--cachedir',str(root/'cache'),'--logdir',str(root/'logs'),'--ffmpeg','/usr/bin/ffmpeg'],stdout=log,stderr=log)
    try:
        for _ in range(180):
            if server.poll() is not None:raise RuntimeError('Jellyfin exited; see server.log')
            try:api('/Startup/Configuration');break
            except (OSError,ValueError):time.sleep(.5)
        else:raise RuntimeError('Jellyfin did not start')
        api('/Startup/Configuration',{'UICulture':'en-US','MetadataCountryCode':'US','PreferredMetadataLanguage':'en'})
        api('/Startup/User')
        api('/Startup/User',{'Name':username,'Password':password})
        api('/Startup/RemoteAccess',{'EnableRemoteAccess':False,'EnableAutomaticPortMapping':False})
        api('/Startup/Complete',{})
        auth=api('/Users/AuthenticateByName',{'Username':username,'Pw':password});token=auth['AccessToken'];uid=auth['User']['Id']
        for name,folder in [('Fixture Music','music'),('Other Music','other-music')]:
            api('/Library/VirtualFolders?'+urllib.parse.urlencode({'name':name,'collectionType':'music','refreshLibrary':'false'}),{'LibraryOptions':{'PathInfos':[{'Path':str(root/folder)}],'EnableRealtimeMonitor':False,'EnableInternetProviders':False,'EnableAutomaticSeriesGrouping':False,'MetadataSavers':[],'TypeOptions':[{'Type':kind,'MetadataFetchers':[],'MetadataFetcherOrder':[],'ImageFetchers':[],'ImageFetcherOrder':[]} for kind in ['MusicAlbum','MusicArtist','Audio']]}})
        api('/Library/Refresh',{})
        for _ in range(240):
            result=api('/Items?'+urllib.parse.urlencode({'UserId':uid,'Recursive':'true','IncludeItemTypes':'Audio','SearchTerm':'Fixture','Limit':200}))
            if len(result.get('Items',[]))==105:break
            time.sleep(.5)
        else:raise RuntimeError('Jellyfin music scan did not finish')
        guest=api('/Users/New',{'Name':'sung-reader','Password':password})
        policy=guest['Policy'];policy.update(IsAdministrator=False,EnableAllFolders=True,EnableMediaPlayback=True,EnableAudioPlaybackTranscoding=True,EnableContentDeletion=False,EnableSharedDeviceControl=False)
        api('/Users/'+guest['Id']+'/Policy',policy)
        env=os.environ|{'SUNG_HELPER':str(Path(__file__).resolve().parents[1]/'helper/catalog.py'),'SUNG_PYTHON':shutil.which('python3'),'SUNG_TEST_SERVER':base,'SUNG_TEST_USER':username,'SUNG_TEST_PASSWORD':password,'SUNG_TEST_READER':'sung-reader','SUNG_TEST_READER_ID':guest['Id'],'SUNG_TEST_ADMIN_ID':uid,'SUNG_TEST_ADMIN_TOKEN':token,'SUNG_TEST_OUTPUT':str(root/'screens'),'SUNG_TEST_LOCAL_FILE':str(root/'other-music/Other Album/other.flac'),'XDG_CONFIG_HOME':str(root/'client-config'),'XDG_DATA_HOME':str(root/'client-data'),'XDG_CACHE_HOME':str(root/'client-cache'),'QT_QPA_PLATFORM':'offscreen','QT_QUICK_BACKEND':'software','QT_QPA_PLATFORMTHEME':'generic','QT_FFMPEG_DECODING_HW_DEVICE_TYPES':',','QT_FFMPEG_ENCODING_HW_DEVICE_TYPES':','}
        # Fixture credentials remain only in this private test output directory.
        (root/'environment.json').write_text(json.dumps({k:v for k,v in env.items() if k.startswith(('SUNG_','XDG_','QT_'))},indent=2));(root/'environment.json').chmod(0o600)
        (root/'server.pid').write_text(str(server.pid))
        if args.prepare_only:print('Fixture server prepared at '+str(root));return
        if not args.test_binary:raise RuntimeError('--test-binary is required unless --prepare-only is selected')
        with (root/'integration.log').open('w') as log:
            result=subprocess.run([str(args.test_binary.resolve())],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=300)
        if result.returncode:raise RuntimeError('Sung Jellyfin integration failed; see integration.log')
        if args.ui_binary:
            if shutil.which('fc-match'):
                font = subprocess.check_output(['fc-match','Google Sans Flex','-f','%{file}'],text=True).strip()
                font_config = root/'fonts.conf'
                font_config.write_text('<?xml version="1.0"?><!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd"><fontconfig><include>/etc/fonts/fonts.conf</include><dir>'+escape(str(Path(font).parent))+'</dir></fontconfig>')
                env['FONTCONFIG_FILE'] = str(font_config)
            if args.native_ui:
                env['QT_QPA_PLATFORM']='wayland'
                env['SUNG_TEST_BACKGROUND_ACTIVATION']='1'
                env.pop('QT_QUICK_BACKEND',None)
                env.pop('QSG_RENDER_LOOP', None)
                wrapper=root/'ui.sh'
                exit_file=root/'ui-exit'
                # Pass only task-specific overrides; retain the desktop session environment.
                keys=[k for k in env if k.startswith(('SUNG_','XDG_CONFIG_HOME','XDG_DATA_HOME','XDG_CACHE_HOME','QT_','FONTCONFIG_FILE'))]
                command=['env']+[k+'='+env[k] for k in keys]+[str(args.ui_binary.resolve()),'--isolated','--jellyfin-test']
                wrapper.write_text("#!/usr/bin/env bash\nunset QT_QUICK_BACKEND QSG_RENDER_LOOP\n"+shlex.join(command)+" > "+shlex.quote(str(root/'ui.log'))+" 2>&1\nresult=$?\nprintf '%s\\n' \"$result\" > "+shlex.quote(str(exit_file))+"\n")
                wrapper.chmod(0o700)
                # The layout checks resize the window. Keep the test
                # surface floating so tiling cannot override its size.
                launch='[workspace 2 silent; float] /usr/bin/bash '+shlex.quote(str(wrapper))
                subprocess.run(['hyprctl','eval','hl.exec_cmd('+json.dumps(launch)+')'],check=True,stdout=subprocess.DEVNULL)
                deadline=time.monotonic()+120
                while not exit_file.exists() and time.monotonic()<deadline:
                    time.sleep(.25)
                if not exit_file.exists() or exit_file.read_text().strip()!='0':
                    raise RuntimeError('Native UI tests failed; see ui.log')
            else:
                with (root/'ui.log').open('w') as testlog:
                    run = subprocess.run([str(args.ui_binary.resolve()), '--jellyfin-test', '--isolated'], env=env, stdout=testlog, stderr=subprocess.STDOUT, timeout=120)
                if run.returncode:
                    raise RuntimeError('Sung UI tests failed; see ui.log')
        (root/'result.json').write_text(json.dumps({'passed':True,'serverVersion':api('/System/Info/Public').get('Version'),'tracks':105,'basePath':True}))
    finally:
        if not args.prepare_only or not (root/'environment.json').exists():
            server.terminate()
            try:server.wait(timeout=15)
            except subprocess.TimeoutExpired:server.kill();server.wait()

if __name__=='__main__':main()
