"""Best-effort public album artwork lookup. No credentials or persistent worker."""
from fractions import Fraction
from datetime import date
from urllib.error import HTTPError
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import time
import unicodedata
from urllib.parse import urlencode, urljoin, urlsplit
from urllib.request import Request, HTTPRedirectHandler, build_opener

MEDIA_LIMIT = 16 * 1024 * 1024
CACHE_LIMIT = 64 * 1024 * 1024
HOSTS = {'itunes.apple.com', 'music.apple.com', 'mvod.itunes.apple.com'}


def safe_url(url):
    p = urlsplit(url)
    if p.scheme != 'https' or p.hostname not in HOSTS or p.username or p.password or p.port not in (None, 443):
        raise ValueError('Unsupported artwork URL')
    return url


class Redirects(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return super().redirect_request(req, fp, code, msg, headers, safe_url(newurl))


def fetch(url, limit=2 * 1024 * 1024):
    with build_opener(Redirects()).open(Request(safe_url(url), headers={
            'User-Agent': 'Mozilla/5.0 (compatible; Sung)', 'Accept-Encoding': 'identity'}), timeout=8) as response:
        if int(response.headers.get('Content-Length', 0)) > limit:
            raise ValueError('Artwork response too large')
        data = response.read(limit + 1)
        if len(data) > limit:
            raise ValueError('Artwork response too large')
        return data


def normal(value):
    return ' '.join(re.sub(r'[^\w]+', ' ', unicodedata.normalize('NFKC', str(value)).casefold()).split())


def title_key(value):
    # Remove presentation labels only; live/remix/remaster variants remain distinct.
    return normal(re.sub(r'\s*[\[(](?:official (?:music )?(?:video|audio)|lyrics?|lyric video)[\])]\s*', ' ', str(value), flags=re.I))


def candidates(track, results):
    artist = normal(re.sub(r'\s+- Topic$', '', track.get('artist', ''), flags=re.I))
    title = title_key(track.get('title', ''))
    album = normal(track.get('album', ''))
    if not artist or not title:
        return []
    matches = []
    for item in results[:201]:
        if not isinstance(item, dict) or normal(item.get('artistName', '')) != artist or title_key(item.get('trackName', '')) != title:
            continue
        if album and normal(item.get('collectionName', '')) != album:
            continue
        seconds = float(track.get('seconds') or 0)
        if seconds and (not math.isfinite(seconds) or abs(float(item.get('trackTimeMillis', 0)) / 1000 - seconds) > 5):
            continue
        if not str(item.get('collectionId', '')).isdigit():
            continue
        matches.append(item)
    # Without album metadata, do not guess between compilations or editions.
    if not album and len({normal(i.get('collectionName', '')) for i in matches}) != 1:
        return []
    unique = {str(i['collectionId']): i for i in matches}
    return list(unique.values())[:3]


def album_key(value):
    return normal(re.sub(r"\s*[\[(](?:deluxe(?: edition| video album)?|expanded edition)[\])]\s*$", '', str(value), flags=re.I))


def resolve_candidates(track, results):
    exact = candidates(track, results)
    if exact:
        return exact
    artist = normal(re.sub(r'\s+- Topic$', '', track.get('artist', ''), flags=re.I))
    album = normal(track.get('album', ''))
    if not artist or not title_key(track.get('title', '')):
        return []
    single = normal(re.sub(r'\s*-\s*Single$', '', track.get('album', ''), flags=re.I)) == title_key(track.get('title', ''))
    if album and not single:
        # A song search can return other songs from the right album. Verify its
        # actual track list before deciding that the requested recording is absent.
        ids = list(dict.fromkeys(str(i['collectionId']) for i in results
            if normal(i.get('artistName', '')) == artist and normal(i.get('collectionName', '')) == album
            and str(i.get('collectionId', '')).isdigit()))[:2]
        if not ids:
            query = urlencode(dict(term=track['artist']+' '+track['album'], media='music', entity='album', limit=20, country='us'))
            albums = json.loads(fetch('https://itunes.apple.com/search?'+query)).get('results', [])
            ids = list(dict.fromkeys(str(i['collectionId']) for i in albums
                if normal(i.get('artistName', '')) == artist and normal(i.get('collectionName', '')) == album
                and str(i.get('collectionId', '')).isdigit()))[:2]
        for album_id in ids:
            tracks = json.loads(fetch('https://itunes.apple.com/lookup?'+urlencode(dict(id=album_id, entity='song', limit=200, country='us')))).get('results', [])
            found = candidates(track, tracks[:201])
            if found:
                return found
        return []
    # Singles and missing album tags may use the recording's original album.
    # Require duration, and disambiguate using album release dates, not search order.
    if not float(track.get('seconds') or 0) > 0:
        return []
    matches = [i for i in results if candidates(dict(track, album=i.get('collectionName', '')), [i])]
    by_id = {str(i['collectionId']): i for i in matches}
    ids = list(by_id)[:8]
    if not ids:
        return []
    albums = json.loads(fetch('https://itunes.apple.com/lookup?'+urlencode(dict(id=','.join(ids), entity='album', country='us')))).get('results', [])
    dated = []
    for album in albums:
        song = by_id.get(str(album.get('collectionId', '')))
        if not song or album.get('wrapperType') != 'collection' or normal(album.get('artistName', '')) != artist:
            continue
        if normal(album.get('collectionName', '')) != normal(song.get('collectionName', '')) or int(album.get('trackCount', 0)) < 2:
            continue
        try:
            released = date.fromisoformat(album.get('releaseDate', '')[:10])
        except ValueError:
            continue
        dated.append((released, album_key(album['collectionName']), song))
    if not dated:
        return []
    earliest = min(x[0] for x in dated)
    originals = [x for x in dated if x[0] == earliest]
    if len({x[1] for x in originals}) != 1:
        return []
    originals.sort(key=lambda x: normal(x[2]['collectionName']) != x[1])
    return [x[2] for x in originals][:3]


def album_motion(raw, candidate):
    match = re.search(r'<script[^>]*id="serialized-server-data"[^>]*>(.*?)</script>', raw.decode(), re.S)
    if not match:
        return ''
    payload = json.loads(match.group(1))
    for page in payload.get('data', [])[:4]:
        for section in page.get('data', {}).get('sections', [])[:12]:
            for item in section.get('items', [])[:20]:
                if (item.get('id') == 'album-detail-header - ' + str(candidate['collectionId'])
                        and normal(item.get('title', '')) == normal(candidate['collectionName'])
                        and [normal(x.get('title', '')) for x in item.get('subtitleLinks', [])] == [normal(candidate['artistName'])]):
                    url = item.get('videoArtwork', {}).get('dictionary', {}).get('motionDetailSquare', {}).get('video', '')
                    return safe_url(url) if url else ''
    return ''


def attributes(line):
    return dict((k, v.strip('"')) for k, v in re.findall(r'([A-Z-]+)=("[^"]*"|[^,]*)', line.partition(':')[2]))


def variant_url(raw, base):
    lines = raw.decode().splitlines()
    choices = []
    for i, line in enumerate(lines[:-1]):
        if not line.startswith('#EXT-X-STREAM-INF:'):
            continue
        a = attributes(line)
        width, height = map(int, a.get('RESOLUTION', '0x0').split('x'))
        if (not 128 <= width == height <= 800 or not a.get('CODECS', '').startswith('avc1')
                or a.get('VIDEO-RANGE', 'SDR') != 'SDR' or float(a.get('FRAME-RATE', '30')) > 30
                or int(a.get('BANDWIDTH', '0')) > 3000000 or lines[i+1].startswith('#')):
            continue
        choices.append((width, safe_url(urljoin(base, lines[i+1].strip()))))
    # Prefer a modest size for the shared decoder, retaining detail in immersive view.
    return min(choices, key=lambda x: abs(x[0]-512))[1] if choices else ''


def movie_url(raw, base):
    lines = raw.decode().splitlines()
    if '#EXT-X-ENDLIST' not in lines or len(lines) > 512:
        raise ValueError('Not a bounded VOD cover')
    urls, duration, segments = set(), 0., 0
    for line in lines:
        if line.startswith('#EXT-X-KEY:') and attributes(line).get('METHOD') != 'NONE':
            raise ValueError('Encrypted cover')
        if line.startswith('#EXT-X-MAP:'):
            urls.add(safe_url(urljoin(base, attributes(line).get('URI', ''))))
        elif line.startswith('#EXTINF:'):
            duration += float(line.split(':')[1].split(',')[0]); segments += 1
        elif line and not line.startswith('#'):
            urls.add(safe_url(urljoin(base, line.strip())))
    if not math.isfinite(duration) or not 0 < duration <= 60 or not 0 < segments <= 64 or len(urls) != 1:
        raise ValueError('Unsupported cover layout')
    url = urls.pop()
    if not urlsplit(url).path.endswith('.mp4'):
        raise ValueError('Unsupported cover container')
    return url


def validate_movie(path):
    with path.open('rb') as source:
        if source.read(12)[4:8] != b'ftyp':
            raise ValueError('Not an MP4 cover')
    result = subprocess.run(['ffprobe', '-v', 'error', '-protocol_whitelist', 'file',
        '-show_entries', 'stream=codec_type,codec_name,width,height,r_frame_rate:format=duration', '-of', 'json', str(path)],
        capture_output=True, timeout=8, check=True)
    info = json.loads(result.stdout)
    streams = info.get('streams', [])
    if (len(streams) != 1 or streams[0].get('codec_type') != 'video' or streams[0].get('codec_name') != 'h264'
            or not 128 <= streams[0].get('width', 0) == streams[0].get('height', 0) <= 800
            or not 0 < float(Fraction(streams[0].get('r_frame_rate', '0'))) <= 30
            or not 0 < float(info.get('format', {}).get('duration', 0)) <= 60):
        raise ValueError('Unsupported cover video')


def cached_movie(path, expected_size=None):
    try:
        if path.is_symlink() or not path.is_file() or not 0 < path.stat().st_size <= MEDIA_LIMIT:
            return False
        if expected_size is not None and path.stat().st_size != expected_size:
            return False
        with path.open('rb') as source:
            return source.read(12)[4:8] == b'ftyp'
    except OSError:
        return False


def prune(cache):
    files = sorted((p for p in cache.glob('*.mp4') if p.is_file()), key=lambda p: p.stat().st_mtime, reverse=True)
    total = 0
    for p in files:
        total += p.stat().st_size
        if total > CACHE_LIMIT:
            p.unlink(missing_ok=True)
    for p in sorted(cache.glob('*.json'), key=lambda p: p.stat().st_mtime, reverse=True)[256:]:
        p.unlink(missing_ok=True)


def lookup(req):
    """All expected failures are quiet. Audio playback never depends on this result."""
    cache = Path(req['artworkCache']); scratch = Path(req['scratch'])
    cache.mkdir(parents=True, exist_ok=True, mode=0o700)
    scratch.mkdir(parents=True, exist_ok=True, mode=0o700)
    key = hashlib.sha256(json.dumps([2]+[req.get(k, '') for k in ('title', 'artist', 'album', 'seconds')]).encode()).hexdigest()
    record = cache / (key + '.json')
    now = time.time()
    try:
        saved = json.loads(record.read_text())
        if saved['expires'] > now and (not req.get('refresh') or saved.get('status') == 'retry'):
            if not saved.get('albumId'):
                return {'status': saved.get('status', 'unavailable'), 'retryAfter': max(1, math.ceil(saved['expires']-now))} if saved.get('status') == 'retry' else {'status': 'unavailable'}
            path = cache / (str(saved['albumId']) + '.mp4')
            if str(saved['albumId']).isdigit() and cached_movie(path, saved.get('bytes')):
                path.touch()
                return {'status': 'ready', 'motionArt': path.resolve().as_uri(), 'page': saved['page']}
            if str(saved['albumId']).isdigit() and path.is_file() and not path.is_symlink():
                path.unlink()
    except (OSError, ValueError, KeyError, TypeError):
        pass
    result = {'status': 'unavailable'}
    saved = {'expires': now + 86400}
    try:
        blocked = cache / 'retry-after-v2'
        if blocked.exists() and float(blocked.read_text()) > now:
            return {'status': 'retry', 'retryAfter': max(1, math.ceil(float(blocked.read_text())-now))}
        query = urlencode({'term': re.sub(r'\s+- Topic$', '', req.get('artist', ''), flags=re.I) + ' ' + title_key(req.get('title', '')), 'media': 'music',
                           'entity': 'song', 'limit': 40, 'country': 'us'})
        matches = resolve_candidates(req, json.loads(fetch('https://itunes.apple.com/search?' + query)).get('results', []))
        for candidate in matches:
            album_id = str(candidate['collectionId'])
            page = 'https://music.apple.com/us/album/' + album_id
            path = cache / (album_id + '.mp4')
            try:
                if cached_movie(path):
                    try:
                        validate_movie(path)
                    except (ValueError, ZeroDivisionError, subprocess.SubprocessError):
                        path.unlink()
                if not cached_movie(path):
                    master = album_motion(fetch(page), candidate)
                    if not master:
                        continue
                    variant = variant_url(fetch(master, 262144), master)
                    if not variant:
                        continue
                    url = movie_url(fetch(variant, 262144), variant)
                    temp = scratch / 'cover.mp4'
                    temp.write_bytes(fetch(url, MEDIA_LIMIT))
                    validate_movie(temp)
                    # A complete silent MP4 needs no transcoding or second decoder.
                    os.replace(temp, path)
            except HTTPError as error:
                if error.code not in (404, 410):
                    raise
                continue
            except (ValueError, KeyError, TypeError, AttributeError, ZeroDivisionError, subprocess.SubprocessError):
                continue
            path.touch()
            result = {'status': 'ready', 'motionArt': path.resolve().as_uri(), 'page': page}
            saved.update(albumId=album_id, page=page, bytes=path.stat().st_size, expires=now + 7 * 86400)
            break
    except (OSError, ValueError, KeyError, TypeError, AttributeError, ZeroDivisionError, subprocess.SubprocessError) as error:
        delay = 30
        if isinstance(error, HTTPError) and error.code == 429:
            try:
                delay = max(30, min(3600, int(error.headers.get('Retry-After', '600'))))
            except (ValueError, TypeError):
                delay = 600
            try:
                (cache / 'retry-after-v2').write_text(str(time.time() + delay))
            except OSError:
                pass
        result = {'status': 'retry', 'retryAfter': delay}
        saved = {'expires': time.time() + delay, 'status': 'retry'}
    temp_record = scratch / 'record.json'
    temp_record.write_text(json.dumps(saved))
    os.replace(temp_record, record)
    prune(cache)
    return result
