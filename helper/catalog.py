#!/usr/bin/env python3
"""One request per process. No server, browser, telemetry, or idle worker."""
import json
import re
import sys
from urllib.parse import urlparse, parse_qs


def artwork(item):
    # Search and browse results carry `thumbnails`. A watch playlist carries
    # `thumbnail`, singular, because ytmusicapi's parse_watch_track writes that
    # key, and radio and autoplay are both built from watch playlists. Reading
    # only the plural left every song they queued without a cover.
    thumbs = item.get('thumbnails') or item.get('thumbnail') or []
    if not thumbs:
        return ''
    src = thumbs[-1].get('url', '')
    # Google returns tiny search thumbnails; ask the same image service for 544px.
    if 'googleusercontent.com/' in src or 'ggpht.com/' in src:
        src = re.sub(r'=w\d+-h\d+[^?]*$', '=w544-h544-l90-rj', src)
    return src


def normalize(item, kind='', parent=None):
    parent = parent or {}
    video = item.get('videoId') or ''
    browse = item.get('browseId') or item.get('playlistId') or ''
    kind = item.get('resultType') or kind or ('song' if video else 'playlist' if item.get('playlistId') else 'album')
    artists = item.get('artists') or parent.get('artists') or []
    if isinstance(artists, str):
        artists = [{'name': artists}]
    album = item.get('album') or {}
    if isinstance(album, str):
        album = {'name': album}
    if parent.get('type') == 'album':
        album = {'name': parent.get('title', ''), 'id': parent.get('browseId', '')}
    author = item.get('author') or {}
    artist_name = ', '.join(x.get('name', '') for x in artists)
    if not artist_name and isinstance(author, dict):
        artist_name = author.get('name', '')
    return {'id': video or browse, 'videoId': video, 'browseId': browse,
            'kind': kind, 'title': item.get('title') or item.get('name') or (item.get('artist') if isinstance(item.get('artist'), str) else '') or 'Untitled',
            'artist': artist_name,
            'artistId': next((a.get('id') for a in artists if a.get('id')), browse if kind == 'artist' else ''),
            'album': album.get('name', ''), 'albumId': album.get('id', ''),
            'art': artwork(item) or artwork(parent), 'duration': item.get('duration') or item.get('length') or '',
            'seconds': item.get('duration_seconds') or 0,
            'discNumber': item.get('discNumber') or item.get('disc_number') or 1,
            'explicit': bool(item.get('isExplicit')), 'available': item.get('isAvailable', True)}


def clean(items, kind='', parent=None):
    return [t for i in items if isinstance(i, dict) and (t := normalize(i, kind, parent))['id']]


def normalize_lyrics(data):
    from dataclasses import asdict, is_dataclass
    if is_dataclass(data): data=asdict(data)
    data=data or {}
    raw=data.get('lyrics') or ''
    if isinstance(raw,str):return {'lyrics':raw,'lines':[]}
    lines=[]
    for line in raw:
        if is_dataclass(line):line=asdict(line)
        if not isinstance(line,dict):continue
        start=line.get('start_time');end=line.get('end_time');text=line.get('text','')
        if not isinstance(start,(int,float)) or start<0 or not isinstance(text,str):continue
        lines.append({'start':int(start),'end':int(end) if isinstance(end,(int,float)) and end>=start else 0,'text':text})
    lines.sort(key=lambda line:line['start'])
    return {'lyrics':'\n'.join(line['text'] for line in lines),'lines':lines}


def lyric_fallback(req):
    """Conservative exact lookup; never guess a live/remix version from its title."""
    import unicodedata
    from urllib.parse import urlencode
    from urllib.request import Request, urlopen
    from urllib.error import HTTPError
    from pathlib import Path
    import time
    duration = float(req.get('seconds') or 0)
    title, artist = req.get('title', ''), req.get('artist', '')
    if not title or not artist or not 1 <= duration <= 3600:
        return None
    cache = Path(req['lyricCache']) if req.get('lyricCache') else None
    blocked = cache / 'retry-after' if cache else None
    if blocked and blocked.exists():
        try:
            if float(blocked.read_text()) > time.time(): return None
        except (ValueError, OSError): pass
    def normal(value):
        return ' '.join(unicodedata.normalize('NFKC', str(value)).casefold().split())
    params = dict(track_name=title, artist_name=artist, duration=duration)
    if req.get('album'): params['album_name'] = req['album']
    request = Request('https://lrclib.net/api/get?' + urlencode(params), headers={'User-Agent': 'Sung/0.11.0 (native Linux music client)', 'Accept': 'application/json'})
    try:
        with urlopen(request, timeout=8) as response:
            raw = response.read(1048577)
            if len(raw) > 1048576: return None
            data = json.loads(raw)
    except HTTPError as exc:
        if exc.code == 429 and blocked:
            from email.utils import parsedate_to_datetime
            retry = exc.headers.get('Retry-After', '600')
            try: delay = float(retry)
            except ValueError:
                try: delay = parsedate_to_datetime(retry).timestamp() - time.time()
                except (ValueError, TypeError): delay = 600
            cache.mkdir(parents=True, exist_ok=True)
            blocked.write_text(str(time.time()+max(1, delay)))
        return None
    if not isinstance(data, dict): return None
    if normal(data.get('trackName')) != normal(title) or normal(data.get('artistName')) != normal(artist): return None
    if abs(float(data.get('duration', 0))-duration) > 2: return None
    if data.get('syncedLyrics') and len(data['syncedLyrics']) <= 262144:
        return {'lrc': data['syncedLyrics'], 'lyrics': data.get('plainLyrics') or '', 'source': 'LRCLIB'}
    return None


# Bits per sample, which ffprobe reports directly for lossless formats and only
# through the decoder's sample format for the rest. A lossy codec has no
# meaningful depth of its own, so it reports none.
def sample_depth(stream):
    raw = stream.get('bits_per_raw_sample')
    try:
        if raw and 0 < int(raw) <= 64: return int(raw)
    except (TypeError, ValueError): pass
    if str(stream.get('codec_name') or '').lower() not in ('flac','alac','wavpack','pcm_s16le','pcm_s24le','pcm_s32le','tta','ape'): return 0
    depths = {'s16':16,'s16p':16,'s32':32,'s32p':32,'fltp':32,'flt':32,'u8':8,'u8p':8}
    return depths.get(str(stream.get('sample_fmt') or '').lower(), 0)


# ReplayGain and R128 loudness tags, mapped to the names the player reads.
GAIN_TAGS = {'replaygain_track_gain':'replaygainTrackGain','replaygain_album_gain':'replaygainAlbumGain','r128_track_gain':'r128TrackGain'}
AUDIO_EXTENSIONS = {'.mp3','.flac','.ogg','.opus','.m4a','.aac','.wav','.aiff','.aif','.wma'}


ART_EXTENSIONS = ('.gif', '.webp', '.mp4', '.webm', '.jpg', '.jpeg', '.png')

def local_cover(path, directories):
    """Track-specific sidecars precede shared album covers; names ignore case."""
    import os
    if path.parent not in directories:
        try:
            matches = {}
            with os.scandir(path.parent) as entries:
                for entry in entries:
                    if not entry.name.lower().endswith(ART_EXTENSIONS) or not entry.is_file(): continue
                    key = entry.name.casefold()
                    if key not in matches or entry.name < matches[key].name:
                        matches[key] = path.parent / entry.name
            directories[path.parent] = matches
        except OSError:
            directories[path.parent] = {}
    names = directories[path.parent]
    for stem in (path.stem, 'cover', 'folder', 'front', 'artwork'):
        for extension in ART_EXTENSIONS:
            candidate = names.get((stem + extension).casefold())
            if candidate: return candidate
    return None

def tag_number(value):
    try: return max(1, min(9999, int(str(value or '1').split('/')[0])))
    except (ValueError, TypeError): return 1


def local_stamp(path, cover):
    st = path.stat()
    stamp = f'{st.st_mtime_ns}:{st.st_size}'
    if cover:
        try:
            st = cover.stat()
            stamp += f'|{cover.name}:{st.st_mtime_ns}:{st.st_size}'
        except OSError: pass
    return stamp

def cover_poster(cover, directory):
    """Cache a bounded poster. Failure must never reject the audio import."""
    import hashlib, os, subprocess
    from pathlib import Path
    try:
        st = cover.stat()
        if st.st_size > 128*1024*1024: return '', ''
        probe = subprocess.run(['ffprobe','-v','error','-protocol_whitelist','file,crypto,data',
                                '-select_streams','v:0','-show_entries','stream=width,height',
                                '-of','json',str(cover)], capture_output=True, timeout=5)
        streams = json.loads(probe.stdout).get('streams', []) if not probe.returncode and len(probe.stdout)<16384 else []
        if not streams or not (0 < streams[0].get('width',0) <= 4096 and 0 < streams[0].get('height',0) <= 4096): return '', ''
        directory = Path(directory); directory.mkdir(parents=True, exist_ok=True)
        identity = hashlib.sha256(os.fsencode(str(cover))).hexdigest()
        target = directory / ('cover_' + identity + '.jpg')
        stamp = f'{st.st_mtime_ns}:{st.st_size}'
        marker = target.with_suffix('.stamp')
        if not target.is_file() or not marker.is_file() or marker.read_text()!=stamp:
            if sum(f.stat().st_size for f in directory.glob('*.jpg')) >= 48*1024*1024 and not target.exists(): return '', ''
            temporary = target.with_suffix('.tmp.jpg')
            try:
                result = subprocess.run(['ffmpeg','-nostdin','-v','error','-threads','1',
                    '-protocol_whitelist','file,crypto,data','-i',str(cover),'-map','0:v:0',
                    '-frames:v','1','-vf','scale=512:512:force_original_aspect_ratio=decrease',
                    '-threads','1','-q:v','4','-y',str(temporary)],capture_output=True,timeout=5)
                if result.returncode or not temporary.is_file() or temporary.stat().st_size>262144: return '', ''
                temporary.replace(target); marker.write_text(stamp)
            finally:
                temporary.unlink(missing_ok=True)
        motion = cover.as_uri()+'?v='+stamp if cover.suffix.lower() in ART_EXTENSIONS[:4] else ''
        return target.as_uri()+'?v='+stamp, motion
    except (OSError, ValueError, subprocess.TimeoutExpired):
        return '', ''

def scan_music_folders(req):
    import os
    from pathlib import Path
    known = req.get('known', {})
    files, failed, seen, visited = [], 0, set(), set()
    count, payload, limited = 0, 0, False
    directories = {}
    def scan(directory, depth=0):
        nonlocal count, payload, limited, failed
        if limited: return
        if depth > 64:
            limited = True
            return
        try:
            canonical = str(Path(directory).resolve())
            if canonical in visited: return
            visited.add(canonical)
            with os.scandir(directory) as entries:
                for entry in entries:
                    count += 1
                    if count > 100000:
                        limited = True
                        return
                    if entry.is_dir(follow_symlinks=False):
                        scan(entry.path, depth+1)
                    elif Path(entry.name).suffix.lower() in AUDIO_EXTENSIONS and entry.is_file():
                        path = Path(entry.path).resolve()
                        if str(path) in seen: continue
                        seen.add(str(path))
                        stamp = local_stamp(path, local_cover(path, directories))
                        if known.get(str(path)) == stamp: continue
                        size = len(str(path).encode('utf-8'))
                        if len(files) >= 10000 or payload+size > 1048576:
                            limited = True
                            return
                        files.append(str(path)); payload += size
                    if limited: return
        except OSError:
            failed += 1
    for directory in req.get('folders', [])[:64]: scan(directory)
    roots = [str(Path(p).resolve()) for p in req.get('folders', [])[:64]]
    missing = [p for p in known if any(p.startswith(r+os.sep) for r in roots) and p not in seen] if not failed and not limited else []
    watches = sorted(visited)[:4096] + sorted(seen)[:10000]
    for root in roots:
        if not Path(root).is_dir():
            ancestor = Path(root).parent
            while not ancestor.exists() and ancestor != ancestor.parent:
                ancestor = ancestor.parent
            watches.append(str(ancestor))
    return {'files': files, 'failed': failed, 'limited': limited, 'missing': missing, 'watchPaths': watches}

def playlist_cleanup(req):
    from pathlib import Path
    import stat
    seen, issues = set(), []
    for index, row in enumerate(req.get('rows', [])):
        path = row.get('localPath', '')
        missing = False
        if path:
            try:
                canonical = str(Path(path).resolve())
                missing = not stat.S_ISREG(Path(path).stat().st_mode)
            except FileNotFoundError:
                canonical = path
                missing = True
            except OSError:
                canonical = path
            key = 'file:' + canonical
        else:
            key = 'youtube:' + (row.get('videoId') or row.get('id') or str(index))
        duplicate = key in seen
        seen.add(key)
        if missing or duplicate: issues.append({'index':index, 'duplicate':duplicate, 'missing':missing})
    return {'issues':issues}


def local_files(req):
    import subprocess, hashlib, math, os
    from pathlib import Path
    allowed = AUDIO_EXTENSIONS
    items, errors = [], []
    directories = {}
    for name in req.get('files', [])[:4]:
        path = Path(name).resolve()
        try:
            if path.suffix.lower() not in allowed or not path.is_file(): raise ValueError('Missing or unsupported audio file')
            probe = subprocess.run(['ffprobe','-v','error','-protocol_whitelist','file,crypto,data','-show_entries','format=duration:format_tags=title,artist,album,album_artist,albumartist,track,disc,date,year,genre,composer,replaygain_track_gain,replaygain_album_gain,r128_track_gain:stream=codec_type,codec_name,sample_rate,bit_rate,channels,bits_per_raw_sample,sample_fmt:stream_tags=genre,composer,replaygain_track_gain,replaygain_album_gain,r128_track_gain:stream_disposition=attached_pic','-of','json',str(path)],capture_output=True,timeout=5)
            if probe.returncode or len(probe.stdout)>262144: raise ValueError('Could not read audio metadata')
            data = json.loads(probe.stdout)
            if not any(stream.get('codec_type')=='audio' for stream in data.get('streams',[])): raise ValueError('No audio stream')
            audio = next(stream for stream in data['streams'] if stream.get('codec_type') == 'audio')
            info = data.get('format',{}); tags = {k.lower():v for k,v in info.get('tags',{}).items()}
            # Vorbis comments live on the audio stream; ID3 lives on the container.
            tags = {**{k.lower():v for k,v in audio.get('tags',{}).items()}, **tags}
            # FLAC and Opus keep loudness tags on the audio stream, MP3 on the container.
            gain = {**{k:str(tags[k])[:32] for k in GAIN_TAGS if tags.get(k)},
                    **{k.lower():str(v)[:32] for k,v in audio.get('tags',{}).items() if k.lower() in GAIN_TAGS and v}}
            seconds = float(info.get('duration') or 0)
            if not math.isfinite(seconds) or seconds<0 or seconds>604800: seconds=0
            identity = 'local_' + hashlib.sha256(os.fsencode(str(path))).hexdigest()
            cover = local_cover(path, directories)
            art, motion = cover_poster(cover, req['artDirectory']) if cover and req.get('artDirectory') else ('', '')
            if not art and req.get('artDirectory') and any(v.get('disposition',{}).get('attached_pic') for v in data.get('streams',[])):
                directory=Path(req['artDirectory']);directory.mkdir(parents=True,exist_ok=True)
                target=directory/(identity+'.jpg')
                if target.exists() or sum(f.stat().st_size for f in directory.glob('*.jpg'))<48*1024*1024:
                    try:
                        extraction=subprocess.run(['ffmpeg','-nostdin','-v','error','-threads','1','-protocol_whitelist','file,crypto,data','-i',str(path),'-map','0:v:0','-frames:v','1','-vf',"scale=512:512:force_original_aspect_ratio=decrease",'-threads','1','-q:v','4','-y',str(target)],capture_output=True,timeout=5)
                        if extraction.returncode==0 and target.is_file() and target.stat().st_size<=262144: art=target.as_uri()+"?v="+str(path.stat().st_mtime_ns)
                        elif target.exists(): target.unlink()
                    except (OSError, subprocess.TimeoutExpired):
                        if target.exists(): target.unlink()
            items.append(dict(**{GAIN_TAGS[k]:v for k,v in gain.items()},id=identity,kind='song',videoId='',localPath=str(path),localStamp=local_stamp(path, cover),title=str(tags.get('title') or path.stem)[:512],artist=str(tags.get('artist') or '')[:512],album=str(tags.get('album') or '')[:512],albumArtist=str(tags.get('album_artist') or tags.get('albumartist') or '')[:512],trackNumber=tag_number(tags.get('track')),discNumber=tag_number(tags.get('disc')),year=str(tags.get('date') or tags.get('year') or '')[:4],seconds=round(seconds),duration=f'{int(seconds)//60}:{int(seconds)%60:02d}' if seconds else '',art=art,motionArt=motion,codec=str(audio.get('codec_name') or '').upper(),sampleRate=int(audio.get('sample_rate') or 0),bitrate=int(audio.get('bit_rate') or 0),channels=int(audio.get('channels') or 0),bitDepth=sample_depth(audio),genre=str(tags.get('genre') or '')[:120],composer=str(tags.get('composer') or '')[:512],available=True))
        except (OSError, ValueError, subprocess.TimeoutExpired):
            errors.append(path.name)
    return {'items':items,'failed':errors}


# What each streaming quality asks YouTube for.
#
# Standard prefers Opus in WebM, which YouTube serves at about 130 kbps: the
# same bytes as the AAC this used to ask for, at better quality per bit. Data
# saver caps the bitrate instead, and YouTube's next Opus rung down is about
# 67 kbps, a little over half the download. A retry after a failed stream
# swaps the container, so it genuinely tries something else.
def audio_format(quality, fallback=False):
    cap = '[abr<=80]' if quality == 'saver' else ''
    containers = ('m4a', 'webm') if fallback else ('webm', 'm4a')
    choices = ['bestaudio[ext=%s]%s' % (c, cap) for c in containers]
    choices.append('bestaudio' + cap)
    if cap:
        # Nothing under the cap means a recording YouTube only offers above it.
        choices.append('worstaudio')
    return '/'.join(choices)


def parse_credentials(raw):
    raw = str(raw or '').strip()
    cookie_map = {}
    auth_user = '0'
    visitor_data = None
    data_sync_id = None
    if '***INNERTUBE COOKIE***' in raw:
        for line in raw.splitlines():
            line = line.strip()
            if line.startswith('***INNERTUBE COOKIE*** ='):
                c_str = line.split('=', 1)[1].strip()
                for part in c_str.split(';'):
                    if '=' in part:
                        k, v = part.strip().split('=', 1)
                        cookie_map[k.strip()] = v.strip()
            elif line.startswith('***VISITOR DATA*** ='):
                visitor_data = line.split('=', 1)[1].strip() or None
            elif line.startswith('***DATASYNC ID*** ='):
                data_sync_id = line.split('=', 1)[1].strip() or None
            elif line.startswith('***AUTH USER*** ='):
                auth_user = line.split('=', 1)[1].strip() or '0'
    elif '# Netscape' in raw or '# HTTP Cookie File' in raw or '\t' in raw:
        for line in raw.splitlines():
            line = line.strip()
            if not line or line.startswith('#'): continue
            parts = line.split('\t')
            if len(parts) >= 7:
                cookie_map[parts[5].strip()] = parts[6].strip()
    elif 'cookie:' in raw.lower():
        for line in raw.splitlines():
            if ':' in line:
                hk, hv = line.split(':', 1)
                hk = hk.strip().lower()
                hv = hv.strip()
                if hk == 'cookie':
                    for part in hv.split(';'):
                        if '=' in part:
                            k, v = part.strip().split('=', 1)
                            cookie_map[k.strip()] = v.strip()
                elif hk == 'x-goog-authuser':
                    auth_user = hv
    elif raw.startswith('{') and raw.endswith('}'):
        try:
            parsed = json.loads(raw)
            if isinstance(parsed, dict):
                c = parsed.get('cookie') or parsed.get('Cookie') or ''
                if isinstance(c, dict): cookie_map.update(c)
                elif isinstance(c, str):
                    for part in c.split(';'):
                        if '=' in part:
                            k, v = part.strip().split('=', 1)
                            cookie_map[k.strip()] = v.strip()
                auth_user = str(parsed.get('authUser') or parsed.get('X-Goog-AuthUser') or parsed.get('x-goog-authuser') or '0')
        except Exception:
            pass
    else:
        for part in raw.split(';'):
            if '=' in part:
                k, v = part.strip().split('=', 1)
                cookie_map[k.strip()] = v.strip()

    sapisid = cookie_map.get('SAPISID') or cookie_map.get('__Secure-3PAPISID') or cookie_map.get('__Secure-1PAPISID')
    return cookie_map, sapisid, auth_user, visitor_data, data_sync_id


def build_auth_headers(auth_input):
    from pathlib import Path
    import hashlib, time
    raw = ''
    if isinstance(auth_input, str):
        p = Path(auth_input)
        if p.is_file():
            try:
                raw = p.read_text(encoding='utf-8', errors='ignore')
            except OSError:
                return None
        else:
            raw = auth_input
    elif isinstance(auth_input, dict):
        raw = json.dumps(auth_input)
    if not raw:
        return None

    cookie_map, sapisid, auth_user, visitor_data, data_sync_id = parse_credentials(raw)
    if not sapisid:
        return None

    cookie_map['SAPISID'] = sapisid
    cookie_map['__Secure-3PAPISID'] = sapisid
    cookie_str = '; '.join(f'{k}={v}' for k, v in cookie_map.items())

    timestamp = int(time.time())
    sapisid_hash = hashlib.sha1(f'{timestamp} {sapisid} https://music.youtube.com'.encode()).hexdigest()
    auth_header = f'SAPISIDHASH {timestamp}_{sapisid_hash}'

    headers = {
        'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36',
        'Accept': '*/*',
        'Accept-Language': 'en-US,en;q=0.9',
        'Content-Type': 'application/json',
        'X-Goog-AuthUser': auth_user or '0',
        'x-origin': 'https://music.youtube.com',
        'Origin': 'https://music.youtube.com',
        'Referer': 'https://music.youtube.com/',
        'Cookie': cookie_str,
        'Authorization': auth_header,
    }
    return headers


def get_ytmusic(req, timeout=20, require_auth=False):
    from ytmusicapi import YTMusic
    auth_input = req.get('auth') or req.get('cookies')
    auth_headers = None
    if auth_input:
        auth_headers = build_auth_headers(auth_input)
    if not auth_headers and req.get('dataPath'):
        from pathlib import Path
        auth_file = Path(req['dataPath']) / 'auth.json'
        if auth_file.is_file():
            auth_headers = build_auth_headers(str(auth_file))
    if require_auth and not auth_headers:
        raise ValueError('Sign in to YouTube Music to sync your library.')
    if auth_headers:
        try:
            api = YTMusic(auth=auth_headers, requests_session=True)
        except Exception as exc:
            raise ValueError('YouTube Music session is invalid or expired. Please sign in again.') from exc
    else:
        api = YTMusic(requests_session=True)
    api._session.request = _timeout_request(api._session.request, timeout)
    return api


def extract_browser_cookies(browser_name=None):
    try:
        import yt_dlp.cookies
    except ImportError:
        return {'ok': False, 'error': 'yt-dlp is not available for browser cookie extraction.'}
    browsers = [browser_name] if browser_name else ['firefox', 'chrome', 'chromium', 'brave', 'edge', 'opera', 'vivaldi']
    AUTH_COOKIE_NAMES = {
        'SAPISID', '__Secure-3PAPISID', '__Secure-1PAPISID',
        'SSID', 'HSID', 'SID', '__Secure-3PSID', '__Secure-1PSID',
        '__Secure-1PSIDTS', '__Secure-3PSIDTS',
        'LOGIN_INFO', 'PREF', 'VISITOR_INFO1_LIVE', 'VISITOR_PRIVACY_METADATA',
        'YSC', 'APISID', 'SIDCC', '__Secure-3PSIDCC', '__Secure-1PSIDCC'
    }
    for b in browsers:
        try:
            jar = yt_dlp.cookies.extract_cookies_from_browser(b)
            cookie_map = {}
            for c in jar:
                if 'youtube.com' in c.domain and c.name in AUTH_COOKIE_NAMES:
                    if getattr(c, 'is_expired', None) and c.is_expired():
                        continue
                    cookie_map[c.name] = c.value
            if cookie_map.get('SAPISID') or cookie_map.get('__Secure-3PAPISID') or cookie_map.get('__Secure-1PAPISID'):
                cookie_str = '; '.join(f'{k}={v}' for k, v in cookie_map.items())
                return {'ok': True, 'browser': b, 'cookies': cookie_str}
        except Exception:
            continue
    return {'ok': False, 'error': 'No active YouTube login session found in installed browsers.'}


def system_browser_login(open_browser=True):
    import webbrowser, time
    # 1. Check if user already has an active session in any installed browser
    existing = extract_browser_cookies()
    if existing.get('ok'):
        return existing

    if not open_browser:
        return {'ok': False, 'error': 'No active YouTube login session found in installed browsers.'}

    # 2. Open the user's default browser to Google / YouTube Music sign-in
    login_url = "https://accounts.google.com/ServiceLogin?continue=https%3A%2F%2Fmusic.youtube.com%2F"
    try:
        webbrowser.open(login_url)
    except Exception:
        pass

    # 3. Wait for the user to complete sign-in in their browser (up to 120 seconds)
    for _ in range(40):
        time.sleep(3)
        check = extract_browser_cookies()
        if check.get('ok'):
            return check

    return {'ok': False, 'error': 'Timed out waiting for sign-in in browser. Please sign in to YouTube Music in your browser and try again.'}


def run(req):
    op = req.get('op', '')
    if op == 'yt-browser-login':
        mode = req.get('mode', 'auto')
        if mode == 'browser':
            login_res = extract_browser_cookies()
        else:
            login_res = system_browser_login(open_browser=True)

        if not login_res.get('ok'):
            return login_res

        account_req = dict(req)
        account_req['op'] = 'yt-account'
        account_req['credentials'] = login_res['cookies']
        res = run(account_req)
        if login_res.get('browser'):
            res['browser'] = login_res['browser']
        return res
    if op == 'yt-account':
        from pathlib import Path
        credentials = req.get('credentials') or req.get('cookies') or ''
        data_path = req.get('dataPath') or ''
        headers = build_auth_headers(credentials)
        if not headers:
            raise ValueError('No active YouTube login session (SAPISID) found in the provided cookies or token.')
        cookie_map, sapisid, auth_user, visitor_data, data_sync_id = parse_credentials(credentials if isinstance(credentials, str) else '')
        from ytmusicapi import YTMusic
        api = YTMusic(auth=headers, requests_session=True)
        api._session.request = _timeout_request(api._session.request, 20)
        account_name = 'YouTube Music User'
        channel_handle = ''
        photo_url = ''
        try:
            info = api.get_account_info()
            account_name = info.get('accountName') or account_name
            channel_handle = info.get('channelHandle') or ''
            photo_url = info.get('accountPhotoUrl') or ''
        except Exception:
            # If account menu parsing fails, probe library to check if session is authenticated
            try:
                api.get_library_playlists(limit=1)
            except Exception:
                raise ValueError('YouTube Music session is invalid or expired. Please sign in again.')
        cookie_file_path = ''
        auth_file_path = ''
        if data_path:
            import os
            dp = Path(data_path)
            dp.mkdir(parents=True, exist_ok=True)
            try:
                os.chmod(dp, 0o700)
            except OSError:
                pass
            auth_file = dp / 'auth.json'
            auth_bytes = json.dumps(headers, indent=2).encode('utf-8')
            fd = os.open(str(auth_file), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
            with open(fd, 'wb') as f:
                f.write(auth_bytes)
            try:
                os.chmod(auth_file, 0o600)
            except OSError:
                pass
            auth_file_path = str(auth_file)
            netscape_lines = ["# Netscape HTTP Cookie File\n"]
            for k, v in cookie_map.items():
                netscape_lines.append(f".youtube.com\tTRUE\t/\tTRUE\t2147483647\t{k}\t{v}\n")
            cookie_file = dp / 'cookies.txt'
            cookie_bytes = "".join(netscape_lines).encode('utf-8')
            fd = os.open(str(cookie_file), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
            with open(fd, 'wb') as f:
                f.write(cookie_bytes)
            try:
                os.chmod(cookie_file, 0o600)
            except OSError:
                pass
            cookie_file_path = str(cookie_file)
        return {
            'ok': True,
            'name': account_name,
            'handle': channel_handle,
            'photo': photo_url,
            'cookieFile': cookie_file_path,
            'authFile': auth_file_path,
        }
    if op == 'yt-sync':
        api = get_ytmusic(req, timeout=40, require_auth=True)
        liked_data = api.get_liked_songs(limit=min(int(req.get('limit', 2000)), 5000))
        liked_tracks = clean(liked_data.get('tracks', []), 'song', liked_data)
        remote_playlists = api.get_library_playlists(limit=100)
        playlists = []
        for p in remote_playlists:
            pid = p.get('playlistId') or p.get('id')
            if not pid or pid in ('LM', 'SE'):
                continue
            try:
                p_data = api.get_playlist(pid, limit=min(int(req.get('playlistLimit', 1000)), 5000))
                raw_tracks = p_data.get('tracks') or []
                raw_count = len(raw_tracks)
                remote_count = p_data.get('trackCount')
                if remote_count is None and 'count' in p_data:
                    remote_count = p_data.get('count')
                if remote_count is None and 'count' in p:
                    remote_count = p.get('count')
                if remote_count is not None:
                    try:
                        if isinstance(remote_count, str):
                            digits = re.search(r'\d+', remote_count)
                            remote_count = int(digits.group(0)) if digits else None
                        else:
                            remote_count = int(remote_count)
                    except (ValueError, TypeError):
                        remote_count = None
                limit = min(int(req.get('playlistLimit', 1000)), 5000)
                if remote_count is not None:
                    is_complete = raw_count >= remote_count
                else:
                    is_complete = raw_count < limit
                p_tracks = clean(raw_tracks, 'song', p_data)
                playlists.append({
                    'id': pid,
                    'browseId': pid,
                    'title': p_data.get('title') or p.get('title') or 'Untitled Playlist',
                    'art': artwork(p_data) or artwork(p),
                    'tracks': p_tracks,
                    'count': len(p_tracks),
                    'complete': is_complete,
                    'isYouTube': True,
                })
            except Exception:
                continue
        return {'ok': True, 'liked': liked_tracks, 'playlists': playlists}
    if op == 'yt-like':
        api = get_ytmusic(req, timeout=15, require_auth=True)
        vid = req.get('id')
        if not vid or not re.fullmatch(r'[A-Za-z0-9_-]{11}', vid):
            raise ValueError('Invalid video ID')
        liked = req.get('liked', True)
        api.rate_song(vid, 'LIKE' if liked else 'INDIFFERENT')
        return {'ok': True}
    if op == 'choose-artwork':
        from pathlib import Path
        cover = Path(req.get('path',''))
        if not cover.is_absolute() or not cover.is_file() or cover.suffix.lower() not in ART_EXTENSIONS[:4]:
            return {'motionArt': ''}
        _, motion = cover_poster(cover, req['artDirectory'])
        return {'motionArt': motion}
    if op == 'online-artwork':
        from online_artwork import lookup
        return lookup(req)
    if op == 'local-files': return local_files(req)
    if op == 'scan-folders': return scan_music_folders(req)
    if op == 'playlist-cleanup': return playlist_cleanup(req)
    if op == 'local-lyrics':
        if req.get('fallback'):
            try: return lyric_fallback(req) or {'lyrics':'','lines':[]}
            except Exception: pass
        return {'lyrics':'','lines':[]}
    if op in ('resolve','buffer'):
        import yt_dlp
        vid = req['id']
        if not re.fullmatch(r'[A-Za-z0-9_-]{11}', vid):
            raise ValueError('Invalid YouTube video ID')
        opts = {'quiet': True, 'noprogress': True, 'no_warnings': True, 'noplaylist': True,
                'format': audio_format(req.get('quality', 'standard'), req.get('fallback')), 'socket_timeout': 18,
                'retries': 2, 'extractor_retries': 2, 'cachedir': False,
                'js_runtimes': {'node': {}}, 'skip_download': True}
        if req.get('cookies'):
            opts['cookiefile'] = req['cookies']
        if op == 'buffer':
            from pathlib import Path
            directory=Path(req['directory']).resolve(strict=True)
            # This path is a C++-created private temporary directory, never a library download.
            for child in directory.iterdir():
                if child.is_file(): child.unlink()
            def bound_size(progress):
                if progress.get('downloaded_bytes',0)>32*1024*1024:
                    raise RuntimeError('This track is too large for temporary buffering')
            opts.update(skip_download=False, max_filesize=32*1024*1024,
                        outtmpl=str(directory/(vid+'.%(ext)s')),progress_hooks=[bound_size])
            with yt_dlp.YoutubeDL(opts) as dl:
                info=dl.extract_info('https://music.youtube.com/watch?v='+vid,download=True)
                path=Path(dl.prepare_filename(info))
            if not path.is_file():
                if (info.get('filesize') or info.get('filesize_approx') or 0)>32*1024*1024:
                    return {'url':info['url'],'headers':info.get('http_headers',{}),'seconds':info.get('duration',0)}
                raise RuntimeError('Could not buffer this track')
            return {'file':str(path),'seconds':info.get('duration',0)}
        with yt_dlp.YoutubeDL(opts) as dl:
            info = dl.extract_info('https://music.youtube.com/watch?v=' + vid, download=False)
        if not info or not info.get('url'):
            raise RuntimeError('No playable audio stream returned')
        return {'url': info['url'], 'headers': info.get('http_headers', {}), 'seconds': info.get('duration', 0)}
    api = get_ytmusic(req, 8 if op == 'lyrics' else 20)
    if op == 'home':
        return {'sections': [{'title': s.get('title', ''), 'items': clean(s.get('contents', []))}
                             for s in api.get_home(limit=5) if s.get('contents')]}
    if op == 'search':
        limit = min(max(int(req.get('limit', 30)), 1), 200)
        return {'items': clean(api.search(req['query'], filter=req.get('filter') or None, limit=limit))}
    if op == 'album':
        data = api.get_album(req['id'])
        data.update(type='album', browseId=req['id'])
        return {'title': data.get('title', ''), 'artist': ', '.join(a.get('name','') for a in data.get('artists',[]) if isinstance(a,dict)), 'year': data.get('year',''), 'art': artwork(data), 'items': clean(data.get('tracks', []), 'song', data)}
    if op == 'playlist':
        data = api.get_playlist(req['id'], limit=min(int(req.get('limit', 100)), 5000))
        return {'title': data.get('title', ''), 'art': artwork(data), 'items': clean(data.get('tracks', []), 'song', data), 'total': data.get('trackCount', 0)}
    if op == 'artist':
        data = api.get_artist(req['id'])
        sections = []
        for key, title, kind in [('songs','Songs','song'),('albums','Albums','album'),('singles','Singles','album'),('videos','Videos','video'),('related','Related artists','artist')]:
            section = data.get(key) or {}
            items = section.get('results', []) if isinstance(section, dict) else section
            if items:
                sections.append({'title':title,'items':clean(items,kind)})
        return {'title':data.get('name',''), 'art':artwork(data), 'sections':sections}
    if op == 'radio':
        data = api.get_watch_playlist(videoId=req['id'], radio=True, limit=30)
        return {'items': clean(data.get('tracks', []), 'song')}
    if op == 'lyrics':
        result = {'lyrics': '', 'lines': [], 'source': 'YouTube'}
        failure = None
        try:
            data = api.get_watch_playlist(videoId=req['id'], limit=1)
            if data.get('lyrics'):
                try: lyrics = api.get_lyrics(data['lyrics'], timestamps=True)
                except Exception: lyrics = api.get_lyrics(data['lyrics'])
                result.update(normalize_lyrics(lyrics))
        except Exception as exc:
            failure = exc
        if not result.get('lines') and req.get('fallback', True):
            try:
                fallback = lyric_fallback(req)
                if fallback: return fallback
            except Exception:
                pass  # Keep usable YouTube lyrics if the secondary service fails.
        if failure and not result['lyrics']: raise failure
        return result
    if op == 'link':
        parsed = urlparse(req['url'])
        if parsed.hostname not in ('youtube.com','www.youtube.com','music.youtube.com','m.youtube.com','youtu.be'):
            raise ValueError('Paste a YouTube or YouTube Music link')
        query = parse_qs(parsed.query)
        video = (query.get('v') or [''])[0]
        if parsed.hostname == 'youtu.be':
            video = parsed.path.strip('/')
        if video:
            data = api.get_song(video).get('videoDetails', {})
            if not data:
                raise ValueError('This song is unavailable')
            t = normalize({'videoId':video,'title':data.get('title'), 'artists':[{'name':data.get('author',''),'id':data.get('channelId','')}], 'thumbnails':data.get('thumbnail',{}).get('thumbnails',[]), 'duration_seconds':int(data.get('lengthSeconds') or 0)}, 'song')
            return {'title':t['title'],'items':[t]}
        playlist = (query.get('list') or [''])[0]
        if playlist:
            return run({'op':'playlist','id':playlist,'limit':100})
        raise ValueError('This link has no song or playlist')
    raise ValueError('Unknown request')


def _timeout_request(original, timeout=20):
    def request(*args, **kwargs):
        kwargs.setdefault('timeout', timeout)
        return original(*args, **kwargs)
    return request


if __name__ == '__main__':
    try:
        payload = sys.stdin.read(16*1024*1024+1)
        if len(payload)>16*1024*1024: raise ValueError('Request is too large')
        data = run(json.loads(payload))
        print(json.dumps({'ok': True, **data}, ensure_ascii=False))
    except Exception as exc:
        print(json.dumps({'ok': False, 'error': str(exc)[-1800:]}))
        sys.exit(1)
