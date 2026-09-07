#!/usr/bin/env python3
"""One request per process. No server, browser, telemetry, or idle worker."""
import json
import re
import sys
from urllib.parse import urlparse, parse_qs


def artwork(item):
    thumbs = item.get('thumbnails') or []
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


AUDIO_EXTENSIONS = {'.mp3','.flac','.ogg','.opus','.m4a','.aac','.wav','.aiff','.aif','.wma'}

def scan_music_folders(req):
    import os
    from pathlib import Path
    known = req.get('known', {})
    files, failed, seen, visited = [], 0, set(), set()
    count, payload, limited = 0, 0, False
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
                        st = path.stat(); stamp = f'{st.st_mtime_ns}:{st.st_size}'
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
    return {'files': files, 'failed': failed, 'limited': limited}

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
    for name in req.get('files', [])[:4]:
        path = Path(name).resolve()
        try:
            if path.suffix.lower() not in allowed or not path.is_file(): raise ValueError('Missing or unsupported audio file')
            probe = subprocess.run(['ffprobe','-v','error','-protocol_whitelist','file,crypto,data','-show_entries','format=duration:format_tags=title,artist,album:stream=codec_type:stream_disposition=attached_pic','-of','json',str(path)],capture_output=True,timeout=5)
            if probe.returncode or len(probe.stdout)>262144: raise ValueError('Could not read audio metadata')
            data = json.loads(probe.stdout)
            if not any(stream.get('codec_type')=='audio' for stream in data.get('streams',[])): raise ValueError('No audio stream')
            info = data.get('format',{}); tags = {k.lower():v for k,v in info.get('tags',{}).items()}
            seconds = float(info.get('duration') or 0)
            if not math.isfinite(seconds) or seconds<0 or seconds>604800: seconds=0
            identity = 'local_' + hashlib.sha256(os.fsencode(str(path))).hexdigest()
            art = ''
            if req.get('artDirectory') and any(v.get('disposition',{}).get('attached_pic') for v in data.get('streams',[])):
                directory=Path(req['artDirectory']);directory.mkdir(parents=True,exist_ok=True)
                target=directory/(identity+'.jpg')
                if target.exists() or sum(f.stat().st_size for f in directory.glob('*.jpg'))<48*1024*1024:
                    try:
                        extraction=subprocess.run(['ffmpeg','-nostdin','-v','error','-threads','1','-protocol_whitelist','file,crypto,data','-i',str(path),'-map','0:v:0','-frames:v','1','-vf',"scale=512:512:force_original_aspect_ratio=decrease",'-threads','1','-q:v','4','-y',str(target)],capture_output=True,timeout=5)
                        if extraction.returncode==0 and target.is_file() and target.stat().st_size<=262144: art=target.as_uri()+"?v="+str(path.stat().st_mtime_ns)
                        elif target.exists(): target.unlink()
                    except (OSError, subprocess.TimeoutExpired):
                        if target.exists(): target.unlink()
            items.append(dict(id=identity,kind='song',videoId='',localPath=str(path),localStamp=f'{path.stat().st_mtime_ns}:{path.stat().st_size}',title=str(tags.get('title') or path.stem)[:512],artist=str(tags.get('artist') or '')[:512],album=str(tags.get('album') or '')[:512],seconds=round(seconds),duration=f'{int(seconds)//60}:{int(seconds)%60:02d}' if seconds else '',art=art,available=True))
        except (OSError, ValueError, subprocess.TimeoutExpired):
            errors.append(path.name)
    return {'items':items,'failed':errors}


def run(req):
    op = req.get('op', '')
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
                'format': ('bestaudio[ext=webm]/bestaudio[ext=m4a]/bestaudio' if req.get('fallback') else 'bestaudio[ext=m4a]/bestaudio'), 'socket_timeout': 18,
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
    from ytmusicapi import YTMusic
    api = YTMusic(requests_session=True)
    # Bound network calls; outer C++ watchdog also terminates stalled operations.
    api._session.request = _timeout_request(api._session.request, 8 if op == 'lyrics' else 20)
    if op == 'home':
        return {'sections': [{'title': s.get('title', ''), 'items': clean(s.get('contents', []))}
                             for s in api.get_home(limit=5) if s.get('contents')]}
    if op == 'search':
        limit = min(max(int(req.get('limit', 30)), 1), 200)
        return {'items': clean(api.search(req['query'], filter=req.get('filter') or None, limit=limit))}
    if op == 'album':
        data = api.get_album(req['id'])
        data.update(type='album', browseId=req['id'])
        return {'title': data.get('title', ''), 'art': artwork(data), 'items': clean(data.get('tracks', []), 'song', data)}
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
