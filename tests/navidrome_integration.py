#!/usr/bin/env python3
"""Run Sung against an isolated real Navidrome server and generated test music."""
import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import secrets
import shlex
import shutil
from xml.sax.saxutils import escape
import socket
import subprocess
import tempfile
import time
import urllib.parse
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--navidrome', required=True, type=Path)
    parser.add_argument('--test-binary', required=True, type=Path)
    parser.add_argument('--ui-binary', type=Path)
    parser.add_argument('--native-ui', action='store_true', help='Run UI checks on Hyprland workspace 2')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    with tempfile.TemporaryDirectory(prefix='sung-navidrome-') as tmp:
        root = Path(tmp)
        music = root / 'music'
        music.mkdir()
        for folder in ('data', 'config', 'cache'):
            (root / folder).mkdir()
        with socket.socket() as sock:
            sock.bind(('127.0.0.1', 0))
            port = sock.getsockname()[1]
        address = f'http://127.0.0.1:{port}'
        username, password = 'sung-fixture', secrets.token_hex(24)
        def encode(index):
            ext = ('flac', 'mp3', 'opus')[index % 3]
            album = music / f'Album {index // 3:03}'
            album.mkdir(exist_ok=True)
            song = album / f'{index:03}.{ext}'
            subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-f', 'lavfi', '-i',
                            'sine=frequency=220:sample_rate=48000', '-t', '12', '-af', 'volume=0.01',
                            '-metadata', f'title=Fixture {index:03}', '-metadata', 'artist=Sung Test Artist',
                            '-metadata', f'album=Test Album {index // 3:03}', '-metadata', 'genre=Test', str(song)], check=True)
            song.with_suffix('.lrc').write_text('[00:00.00]First test line\n[00:04.00]Second test line\n[00:08.00]Last test line\n')
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            list(pool.map(encode, range(105)))
        subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-f', 'lavfi', '-i', 'color=c=0x88ccee:s=96x96', '-frames:v', '1', str(music/'cover.jpg')], check=True)
        for album in music.iterdir():
            if album.is_dir():
                (album/'cover.jpg').write_bytes((music/'cover.jpg').read_bytes())
        config = root / 'navidrome.toml'
        config.write_text(f'Address = "127.0.0.1"\nPort = {port}\nMusicFolder = {json.dumps(str(music))}\nDataFolder = {json.dumps(str(root/"data"))}\nCacheFolder = {json.dumps(str(root/"cache"))}\nScanSchedule = "0"\nEnableInsightsCollector = false\nLogLevel = "error"\n')
        def request(path, body=None):
            req = urllib.request.Request(address + path, data=None if body is None else json.dumps(body).encode(), headers={'Content-Type':'application/json'})
            with urllib.request.urlopen(req, timeout=5) as response:
                return json.load(response)
        def api(method, **params):
            salt = secrets.token_hex(8)
            params.update(u=username, s=salt, t=hashlib.md5((password+salt).encode()).hexdigest(), v='1.16.1', c='SungTest', f='json')
            return request('/rest/'+method+'.view?'+urllib.parse.urlencode(params))['subsonic-response']
        with (output/'server.log').open('w') as log:
            server = subprocess.Popen([str(args.navidrome.resolve()), '--configfile', str(config)], stdout=log, stderr=log)
            try:
                for _ in range(120):
                    if server.poll() is not None:
                        raise RuntimeError('Navidrome exited; see server.log')
                    try:
                        request('/auth/createAdmin', {'username':username, 'password':password})
                        break
                    except (OSError, ValueError):
                        time.sleep(.25)
                else:
                    raise RuntimeError('Could not create fixture account')
                api('startScan')
                for _ in range(180):
                    result = api('search3', query='Fixture', songCount=200, albumCount=0, artistCount=0)
                    if len(result.get('searchResult3',{}).get('song',[])) == 105:
                        break
                    time.sleep(.25)
                else:
                    raise RuntimeError('Fixture library did not finish scanning')
                env = os.environ | {'SUNG_TEST_SERVER':address, 'SUNG_TEST_USER':username, 'SUNG_TEST_PASSWORD':password,
                    'XDG_CONFIG_HOME':str(root/'config'), 'XDG_DATA_HOME':str(root/'client-data'), 'XDG_CACHE_HOME':str(root/'client-cache'),
                    'QT_QPA_PLATFORM':'offscreen', 'QT_QUICK_BACKEND':'software', 'QSG_RENDER_LOOP':'basic',
                    'QT_QPA_PLATFORMTHEME':'generic', 'QT_FORCE_STDERR_LOGGING':'1',
                    'QT_FFMPEG_DECODING_HW_DEVICE_TYPES':',', 'QT_FFMPEG_ENCODING_HW_DEVICE_TYPES':','}
                with (output/'integration.log').open('w') as testlog:
                    run = subprocess.run([str(args.test_binary.resolve())], env=env, stdout=testlog, stderr=subprocess.STDOUT, timeout=180)
                if run.returncode:
                    raise RuntimeError('Sung integration tests failed; see integration.log')
                if args.ui_binary:
                    env['SUNG_TEST_OUTPUT'] = str(output/'screens')
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
                        keys=[k for k in env if k.startswith(('SUNG_TEST_','XDG_CONFIG_HOME','XDG_DATA_HOME','XDG_CACHE_HOME','QT_','FONTCONFIG_FILE'))]
                        command=['env']+[k+'='+env[k] for k in keys]+[str(args.ui_binary.resolve()),'--isolated','--server-test']
                        wrapper.write_text("#!/usr/bin/env bash\nunset QT_QUICK_BACKEND QSG_RENDER_LOOP\n"+shlex.join(command)+" > "+shlex.quote(str(output/'ui.log'))+" 2>&1\nresult=$?\nprintf '%s\\n' \"$result\" > "+shlex.quote(str(exit_file))+"\n")
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
                        with (output/'ui.log').open('w') as testlog:
                            run = subprocess.run([str(args.ui_binary.resolve()), '--server-test', '--isolated'], env=env, stdout=testlog, stderr=subprocess.STDOUT, timeout=120)
                        if run.returncode:
                            raise RuntimeError('Sung UI tests failed; see ui.log')
                (output/'result.json').write_text(json.dumps({'status':'pass','server':'Navidrome','songs':105},indent=2))
                print('PASS: real Navidrome integration; reports in', output)
            finally:
                server.terminate()
                try:
                    server.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    server.kill(); server.wait()

if __name__ == '__main__':
    main()
