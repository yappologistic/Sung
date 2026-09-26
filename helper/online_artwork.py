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
# Room for a few large covers beside the standard ones; the newest stay.
CACHE_LIMIT = 256 * 1024 * 1024
# Two sizes of animated cover. The standard one is what every surface shares,
# small enough that the one decoder costs little beside a 360px still. The
# large one is for the Motion layout, where the cover fills the window, and is
# asked for only while that layout shows. It takes the largest square Apple
# offers up to 2048, which a 1080p or 1440p window crops rather than enlarges.
QUALITIES = {
    'standard': dict(suffix='', largest=800, target=512, bandwidth=3000000, limit=MEDIA_LIMIT, bytes='bytes', richest=False),
    'high': dict(suffix='-hq', largest=2048, target=2048, bandwidth=20000000, limit=64 * 1024 * 1024, bytes='hqBytes', richest=True),
}
# Apple's H.264 ladder for a motion cover stops at 1080 square, in three
# bitrates; only HEVC goes on to 2160 (Innerlight EP, checked 2026-09). The
# large cover takes the highest of the three, where the standard one keeps
# the first listed.
HOSTS = {'itunes.apple.com', 'music.apple.com', 'mvod.itunes.apple.com'}
# A cover on Apple's image service in the shape its search API returns it. The
# player rewrites the size segment for whatever surface draws it.
APPLE_ART = re.compile(r'^https://is\d+-ssl\.mzstatic\.com/image/thumb/[^?#@]+/\d+x\d+bb\.(?:jpg|png|webp)$')
# The second place to look when Apple has no album for a song. MusicBrainz
# names the release group; the Cover Art Archive holds the picture and serves
# it from the Internet Archive, so a redirect there is expected.
COVER_HOSTS = {'musicbrainz.org', 'coverartarchive.org'}
ARCHIVE_HOST = re.compile(r'^(?:[a-z0-9-]+\.)*archive\.org$')
# MusicBrainz asks every client to identify itself and to name a contact.
COVER_AGENT = 'Sung/0.12.0 ( https://github.com/yappologistic/Sung )'
# Its covers are scans people uploaded, so they run from postage stamps to
# full sleeves. Below this a video frame is the better picture of the two.
COVER_FLOOR = 500


def safe_url(url):
    p = urlsplit(url)
    if p.scheme != 'https' or p.hostname not in HOSTS or p.username or p.password or p.port not in (None, 443):
        raise ValueError('Unsupported artwork URL')
    return url


def safe_cover_url(url):
    p = urlsplit(url)
    host = p.hostname or ''
    if (p.scheme != 'https' or p.username or p.password or p.port not in (None, 443)
            or not (host in COVER_HOSTS or ARCHIVE_HOST.match(host))):
        raise ValueError('Unsupported cover URL')
    return url


class Redirects(HTTPRedirectHandler):
    def __init__(self, guard=None):
        self.guard = guard or safe_url

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return super().redirect_request(req, fp, code, msg, headers, self.guard(newurl))


def fetch(url, limit=2 * 1024 * 1024):
    with build_opener(Redirects()).open(Request(safe_url(url), headers={
            'User-Agent': 'Mozilla/5.0 (compatible; Sung)', 'Accept-Encoding': 'identity'}), timeout=8) as response:
        if int(response.headers.get('Content-Length', 0)) > limit:
            raise ValueError('Artwork response too large')
        data = response.read(limit + 1)
        if len(data) > limit:
            raise ValueError('Artwork response too large')
        return data


def fetch_cover(url, limit):
    """The MusicBrainz and Cover Art Archive side, which has its own hosts."""
    with build_opener(Redirects(safe_cover_url)).open(Request(safe_cover_url(url), headers={
            'User-Agent': COVER_AGENT, 'Accept-Encoding': 'identity'}), timeout=10) as response:
        if int(response.headers.get('Content-Length', 0)) > limit:
            raise ValueError('Cover response too large')
        data = response.read(limit + 1)
        if len(data) > limit:
            raise ValueError('Cover response too large')
        return data


def image_size(data):
    """Width and height from a JPEG or PNG header, or None when neither."""
    if data[:8] == b'\x89PNG\r\n\x1a\n' and len(data) >= 24:
        width, height = int.from_bytes(data[16:20], 'big'), int.from_bytes(data[20:24], 'big')
        return (width, height) if width and height else None
    if data[:2] != b'\xff\xd8':
        return None
    at = 2
    while at < len(data) - 9:
        if data[at] != 0xFF:
            at += 1
            continue
        marker = data[at + 1]
        # The frame headers carry the dimensions; everything else is skipped by
        # its own length, which is how the comment and thumbnail blocks pass by.
        if marker in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
            height = int.from_bytes(data[at + 5:at + 7], 'big')
            width = int.from_bytes(data[at + 7:at + 9], 'big')
            return (width, height) if width and height else None
        if marker in (0xD8, 0xD9) or 0xD0 <= marker <= 0xD7:
            at += 2
            continue
        at += 2 + int.from_bytes(data[at + 2:at + 4], 'big')
    return None


def lucene(value):
    return re.sub(r'([+\-&|!(){}\[\]^"~*?:\\/])', r'\\\1', str(value))


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


def still_art(candidate):
    """The album cover a search result links, or '' when it is not one of Apple's images."""
    url = str(candidate.get('artworkUrl100', ''))
    return url if APPLE_ART.match(url) else ''


def archive_cover(track):
    """The album cover the Cover Art Archive holds for this recording, or ''.

    The same rule as the Apple side decides what counts as the right recording:
    the artist and the title have to match once normalised, and the length has
    to agree. A cover smaller than the video frame it would replace is left
    alone, because swapping a frame for a thumbnail is not an improvement."""
    artist = re.sub(r'\s+- Topic$', '', track.get('artist', ''), flags=re.I)
    title = title_key(track.get('title', ''))
    seconds = float(track.get('seconds') or 0)
    if not normal(artist) or not title or not 1 <= seconds <= 3600:
        return ''
    query = urlencode({'query': 'artist:"%s" AND recording:"%s"' % (lucene(artist), lucene(title)),
                       'fmt': 'json', 'limit': 8})
    found = json.loads(fetch_cover('https://musicbrainz.org/ws/2/recording?' + query, 262144))
    groups = []
    for recording in found.get('recordings', [])[:8]:
        if normal(recording.get('title', '')) != normal(title):
            continue
        if not any(normal(c.get('artist', {}).get('name', '')) == normal(artist)
                   for c in recording.get('artist-credit', [])):
            continue
        if abs(float(recording.get('length') or 0) / 1000 - seconds) > 5:
            continue
        for release in recording.get('releases', [])[:4]:
            group = (release.get('release-group') or {}).get('id', '')
            if re.fullmatch(r'[0-9a-f-]{36}', group) and group not in groups:
                groups.append(group)
    for group in groups[:3]:
        url = 'https://coverartarchive.org/release-group/%s/front' % group
        try:
            size = image_size(fetch_cover(url, 262144))
        except HTTPError as error:
            if error.code in (404, 400):
                continue
            raise
        except (OSError, ValueError):
            continue
        if size and min(size) >= COVER_FLOOR:
            return url
    return ''


def album_motion(raw, candidate):
    match = re.search(r'<script[^>]*id="serialized-server-data"[^>]*>(.*?)</script>', raw.decode(), re.S)
    if not match:
        return ''
    payload = json.loads(match.group(1))
    for page in payload.get('data', [])[:4]:
        for section in page.get('data', {}).get('sections', [])[:12]:
            for item in section.get('items', [])[:20]:
                # The header credits the album's artist, which a song with a
                # featured artist does not share: "Elderbrook & Bob Moses" on
                # Elderbrook's Innerlight EP. Search names it separately.
                album_artist = candidate.get('collectionArtistName') or candidate['artistName']
                if (item.get('id') == 'album-detail-header - ' + str(candidate['collectionId'])
                        and normal(item.get('title', '')) == normal(candidate['collectionName'])
                        and [normal(x.get('title', '')) for x in item.get('subtitleLinks', [])] == [normal(album_artist)]):
                    url = item.get('videoArtwork', {}).get('dictionary', {}).get('motionDetailSquare', {}).get('video', '')
                    return safe_url(url) if url else ''
    return ''


def attributes(line):
    return dict((k, v.strip('"')) for k, v in re.findall(r'([A-Z-]+)=("[^"]*"|[^,]*)', line.partition(':')[2]))


def variant_url(raw, base, quality='standard'):
    q = QUALITIES[quality]
    lines = raw.decode().splitlines()
    choices = []
    for i, line in enumerate(lines[:-1]):
        if not line.startswith('#EXT-X-STREAM-INF:'):
            continue
        a = attributes(line)
        width, height = map(int, a.get('RESOLUTION', '0x0').split('x'))
        if (not 128 <= width == height <= q['largest'] or not a.get('CODECS', '').startswith('avc1')
                or a.get('VIDEO-RANGE', 'SDR') != 'SDR' or float(a.get('FRAME-RATE', '30')) > 30
                or int(a.get('BANDWIDTH', '0')) > q['bandwidth'] or lines[i+1].startswith('#')):
            continue
        choices.append((width, safe_url(urljoin(base, lines[i+1].strip())), int(a.get('BANDWIDTH', '0'))))
    return min(choices, key=lambda x: (abs(x[0]-q['target']), -x[2] if q['richest'] else 0))[1] if choices else ''


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


def validate_movie(path, quality='standard'):
    with path.open('rb') as source:
        if source.read(12)[4:8] != b'ftyp':
            raise ValueError('Not an MP4 cover')
    result = subprocess.run(['ffprobe', '-v', 'error', '-protocol_whitelist', 'file',
        '-show_entries', 'stream=codec_type,codec_name,width,height,r_frame_rate:format=duration', '-of', 'json', str(path)],
        capture_output=True, timeout=8, check=True)
    info = json.loads(result.stdout)
    streams = info.get('streams', [])
    if (len(streams) != 1 or streams[0].get('codec_type') != 'video' or streams[0].get('codec_name') != 'h264'
            or not 128 <= streams[0].get('width', 0) == streams[0].get('height', 0) <= QUALITIES[quality]['largest']
            or not 0 < float(Fraction(streams[0].get('r_frame_rate', '0'))) <= 30
            or not 0 < float(info.get('format', {}).get('duration', 0)) <= 60):
        raise ValueError('Unsupported cover video')


def cached_movie(path, expected_size=None, limit=MEDIA_LIMIT):
    try:
        if path.is_symlink() or not path.is_file() or not 0 < path.stat().st_size <= limit:
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
    """All expected failures are quiet. Audio playback never depends on this result.

    One search answers two questions: the album's still cover, which any surface
    can draw in place of a video frame, and, when `motion` is asked for, its
    animated cover. The record remembers whether the animation was looked for,
    so a lookup that skipped it never stands in for one that tried."""
    cache = Path(req['artworkCache']); scratch = Path(req['scratch'])
    cache.mkdir(parents=True, exist_ok=True, mode=0o700)
    scratch.mkdir(parents=True, exist_ok=True, mode=0o700)
    motion = bool(req.get('motion', True))
    covers = bool(req.get('covers', True))
    quality = req.get('quality') if req.get('quality') in QUALITIES else 'standard'
    q = QUALITIES[quality]
    earlier = {}
    key = hashlib.sha256(json.dumps([4]+[req.get(k, '') for k in ('title', 'artist', 'album', 'seconds')]).encode()).hexdigest()
    record = cache / (key + '.json')
    now = time.time()
    try:
        saved = earlier = json.loads(record.read_text())
        if saved['expires'] > now and (not req.get('refresh') or saved.get('status') == 'retry'):
            if saved.get('status') == 'retry':
                return {'status': 'retry', 'retryAfter': max(1, math.ceil(saved['expires']-now))}
            still = {'art': saved.get('art', ''), 'page': saved.get('page', '')}
            album_id = str(saved.get('albumId', ''))
            path = cache / (album_id + q['suffix'] + '.mp4')
            if album_id.isdigit():
                if cached_movie(path, saved.get(q['bytes']), q['limit']):
                    path.touch()
                    return {'status': 'ready', 'motionArt': path.resolve().as_uri(), **still}
                if path.is_file() and not path.is_symlink():
                    path.unlink()
                if not motion:
                    return {'status': 'unavailable', **still}
            elif (not motion or saved.get('motion', True)) and (still['art'] or not covers or saved.get('covers', True)):
                return {'status': 'unavailable', **still}
    except (OSError, ValueError, KeyError, TypeError):
        pass
    result = {'status': 'unavailable', 'art': '', 'page': ''}
    saved = {'expires': now + 86400, 'motion': motion, 'covers': covers}
    try:
        blocked = cache / 'retry-after-v2'
        if blocked.exists() and float(blocked.read_text()) > now:
            return {'status': 'retry', 'retryAfter': max(1, math.ceil(float(blocked.read_text())-now))}
        query = urlencode({'term': re.sub(r'\s+- Topic$', '', req.get('artist', ''), flags=re.I) + ' ' + title_key(req.get('title', '')), 'media': 'music',
                           'entity': 'song', 'limit': 40, 'country': 'us'})
        matches = resolve_candidates(req, json.loads(fetch('https://itunes.apple.com/search?' + query)).get('results', []))
        for candidate in matches:
            # The first verified match names the still cover and the album page; an
            # animated match below replaces both with its own album's.
            if still_art(candidate):
                result.update(art=still_art(candidate), page='https://music.apple.com/us/album/' + str(candidate['collectionId']))
                saved['expires'] = now + 7 * 86400
                break
        if covers and not result['art']:
            # Apple knows nothing about a good deal of what plays here: game
            # soundtracks, fan uploads, releases that never reached a store.
            # MusicBrainz and its Cover Art Archive carry some of them.
            result['art'] = archive_cover(req)
            if result['art']:
                saved['expires'] = now + 7 * 86400
        for candidate in (matches if motion else []):
            album_id = str(candidate['collectionId'])
            page = 'https://music.apple.com/us/album/' + album_id
            path = cache / (album_id + q['suffix'] + '.mp4')
            try:
                if cached_movie(path, limit=q['limit']):
                    try:
                        validate_movie(path, quality)
                    except (ValueError, ZeroDivisionError, subprocess.SubprocessError):
                        path.unlink()
                if not cached_movie(path, limit=q['limit']):
                    master = album_motion(fetch(page), candidate)
                    if not master:
                        continue
                    variant = variant_url(fetch(master, 262144), master, quality)
                    if not variant:
                        continue
                    url = movie_url(fetch(variant, 262144), variant)
                    temp = scratch / 'cover.mp4'
                    temp.write_bytes(fetch(url, q['limit']))
                    validate_movie(temp, quality)
                    # A complete silent MP4 needs no transcoding or second decoder.
                    os.replace(temp, path)
            except HTTPError as error:
                if error.code not in (404, 410):
                    raise
                continue
            except (ValueError, KeyError, TypeError, AttributeError, ZeroDivisionError, subprocess.SubprocessError):
                continue
            path.touch()
            result.update(status='ready', motionArt=path.resolve().as_uri(), page=page, art=still_art(candidate) or result['art'])
            # The other size of the same album stays valid beside this one.
            if earlier.get('albumId') == album_id:
                saved.update({k: earlier[k] for k in ('bytes', 'hqBytes') if k in earlier})
            saved.update({'albumId': album_id, q['bytes']: path.stat().st_size, 'expires': now + 7 * 86400})
            break
        saved.update(art=result['art'], page=result['page'])
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
