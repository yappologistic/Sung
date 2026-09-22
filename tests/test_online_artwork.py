"""Deterministic matching, transport boundaries, cache and real decoder-input tests."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('online_artwork', Path(__file__).parents[1]/'helper/online_artwork.py')
art = importlib.util.module_from_spec(spec); spec.loader.exec_module(art)

class OnlineArtworkTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.track = dict(title='A Song', artist='An Artist', album='An Album', seconds=200,
                          artworkCache=str(self.root/'cache'), scratch=str(self.root/'scratch'))
        self.cover = 'https://is1-ssl.mzstatic.com/image/thumb/Music/ab/cd/100x100bb.jpg'
        self.candidate = dict(trackName='A Song', artistName='An Artist', collectionName='An Album',
                              trackTimeMillis=200000, collectionId=123, artworkUrl100=self.cover)
        self.base = 'https://mvod.itunes.apple.com/example/'

    def page(self):
        item = dict(id='album-detail-header - 123', title='An Album', subtitleLinks=[dict(title='An Artist')],
                    videoArtwork=dict(dictionary=dict(motionDetailSquare=dict(video=self.base+'master.m3u8'))))
        return ('<script id="serialized-server-data">'+json.dumps(dict(data=[dict(data=dict(sections=[dict(items=[item])]))]))+'</script>').encode()

    def test_conservative_matching(self):
        self.assertEqual(art.candidates(self.track, [self.candidate]), [self.candidate])
        self.track['title']='A Song (Official Music Video)'
        self.assertEqual(art.candidates(self.track,[self.candidate]),[self.candidate])
        for field,value in [('artistName','Another Artist'),('trackName','A Song (Live)'),('collectionName','Another Album'),('trackTimeMillis',210000)]:
            self.assertEqual(art.candidates(self.track,[dict(self.candidate,**{field:value})]),[])
        self.track['album']=''
        self.assertEqual(art.candidates(self.track,[self.candidate,dict(self.candidate,collectionName='Compilation',collectionId=124)]),[])
        self.track['artist']=''
        self.assertEqual(art.candidates(self.track,[self.candidate]),[])

    def test_still_cover_only_from_apples_own_images(self):
        self.assertEqual(art.still_art(self.candidate),self.cover)
        for bad in ['http://is1-ssl.mzstatic.com/image/thumb/x/100x100bb.jpg',
                    'https://mzstatic.com.evil.example/image/thumb/x/100x100bb.jpg',
                    'https://is1-ssl.mzstatic.com/image/thumb/x/100x100bb.jpg?x=1',
                    'https://is1-ssl.mzstatic.com/image/thumb/x/cover.gif',
                    'https://is1-ssl.mzstatic.com/image/thumb/x@y/100x100bb.jpg','']:
            self.assertEqual(art.still_art(dict(self.candidate,artworkUrl100=bad)),'',bad)
        self.assertEqual(art.still_art({}),'')

    def test_cover_lookup_skips_the_album_page(self):
        calls=[]
        def fetch(url,limit=0):
            calls.append(url)
            if '/search?' in url:return json.dumps(dict(results=[self.candidate])).encode()
            raise AssertionError('a cover lookup must not fetch '+url)
        with patch.object(art,'fetch',side_effect=fetch):
            first=dict(self.track,motion=False)
            found=art.lookup(first)
            self.assertEqual((found['status'],found['art']),('unavailable',self.cover))
            self.assertEqual(found['page'],'https://music.apple.com/us/album/123')
            self.assertEqual(len(calls),1)
            # Remembered, so playing the song again asks Apple nothing.
            self.assertEqual(art.lookup(first)['art'],self.cover)
            self.assertEqual(len(calls),1)
        # A cover-only answer must not stand in for one that looked for animation.
        with patch.object(art,'fetch',side_effect=OSError('offline')) as fetch:
            self.assertEqual(art.lookup(self.track)['status'],'retry')
            self.assertEqual(fetch.call_count,1)

    def test_cover_hosts_and_redirects(self):
        for good in ['https://musicbrainz.org/ws/2/recording?query=x',
                     'https://coverartarchive.org/release-group/abc/front',
                     'https://ia800207.us.archive.org/12/items/mbid-x/mbid-x-1.jpg']:
            self.assertEqual(art.safe_cover_url(good), good)
        for bad in ['http://coverartarchive.org/x', 'https://coverartarchive.org.evil.example/x',
                    'https://archive.org.evil.example/x', 'https://user@musicbrainz.org/x',
                    'https://musicbrainz.org:8443/x', 'https://itunes.apple.com/search?x=1']:
            with self.assertRaises(ValueError, msg=bad): art.safe_cover_url(bad)
            with self.assertRaises(ValueError, msg=bad):
                art.Redirects(art.safe_cover_url).redirect_request(None, None, 302, '', {}, bad)
        # The two sides keep their own hosts: neither will follow into the other.
        with self.assertRaises(ValueError): art.safe_url('https://coverartarchive.org/x')
        with self.assertRaises(ValueError): art.safe_cover_url('https://itunes.apple.com/search')

    def test_image_size_reads_real_headers(self):
        import subprocess
        # Even numbers only: the generator rounds an odd size down to one.
        for name, size in [('probe.jpg', (640, 480)), ('probe.png', (320, 122))]:
            path = self.root/name
            subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-f', 'lavfi',
                            '-i', 'testsrc2=size=%dx%d' % size, '-frames:v', '1', str(path)],
                           check=True, capture_output=True, timeout=20)
            self.assertEqual(art.image_size(path.read_bytes()), size, name)
        self.assertIsNone(art.image_size(b'not an image at all'))
        self.assertIsNone(art.image_size(b''))

    def test_archive_cover_verifies_the_recording_and_the_size(self):
        from unittest.mock import patch
        import subprocess
        big, small = self.root/'big.jpg', self.root/'small.jpg'
        for path, size in [(big, 900), (small, 250)]:
            subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-f', 'lavfi',
                            '-i', 'testsrc2=size=%dx%d' % (size, size), '-frames:v', '1', str(path)],
                           check=True, capture_output=True, timeout=20)
        group = '11111111-2222-3333-4444-555555555555'
        recording = dict(title='A Song', length=200000, **{'artist-credit': [dict(artist=dict(name='An Artist'))]},
                         releases=[dict(**{'release-group': dict(id=group)})])
        track = dict(title='A Song', artist='An Artist', seconds=200)
        front = 'https://coverartarchive.org/release-group/%s/front' % group
        def answer(recordings, image):
            def fetch(url, limit):
                if 'musicbrainz.org' in url: return json.dumps(dict(recordings=recordings)).encode()
                return image.read_bytes()
            return fetch
        with patch.object(art, 'fetch_cover', side_effect=answer([recording], big)):
            self.assertEqual(art.archive_cover(track), front)
        # A cover smaller than the frame it would replace is left where it is.
        with patch.object(art, 'fetch_cover', side_effect=answer([recording], small)):
            self.assertEqual(art.archive_cover(track), '')
        # The recording itself has to be the right one.
        for wrong in [dict(recording, title='A Song (Live)'),
                      dict(recording, length=260000),
                      dict(recording, **{'artist-credit': [dict(artist=dict(name='Someone Else'))]}),
                      dict(recording, releases=[dict(**{'release-group': dict(id='not-an-mbid')})])]:
            with patch.object(art, 'fetch_cover', side_effect=answer([wrong], big)):
                self.assertEqual(art.archive_cover(track), '')
        # And a song with no length to check cannot be matched at all.
        with patch.object(art, 'fetch_cover', side_effect=answer([recording], big)) as fetch:
            self.assertEqual(art.archive_cover(dict(track, seconds=0)), '')
            fetch.assert_not_called()

    def test_archive_is_the_fallback_not_the_first_stop(self):
        from unittest.mock import patch
        with patch.object(art, 'fetch', return_value=json.dumps(dict(results=[self.candidate])).encode()):
            with patch.object(art, 'archive_cover', return_value='') as archive:
                found = art.lookup(dict(self.track, motion=False))
                self.assertEqual(found['art'], self.cover)
                archive.assert_not_called()
        # Apple knows nothing: the archive is asked, and only when covers are wanted.
        self.track['title'] = 'Something Else'
        front = 'https://coverartarchive.org/release-group/11111111-2222-3333-4444-555555555555/front'
        with patch.object(art, 'fetch', return_value=b'{"results":[]}'):
            with patch.object(art, 'archive_cover', return_value=front) as archive:
                self.assertEqual(art.lookup(dict(self.track, motion=False))['art'], front)
                self.assertEqual(archive.call_count, 1)
                # Remembered, so the next play asks neither service.
                self.assertEqual(art.lookup(dict(self.track, motion=False))['art'], front)
                self.assertEqual(archive.call_count, 1)
            self.track['title'] = 'A Third Thing'
            with patch.object(art, 'archive_cover', return_value=front) as archive:
                self.assertEqual(art.lookup(dict(self.track, motion=False, covers=False))['art'], '')
                archive.assert_not_called()
                # A run that skipped the archive does not stand in for one that wants it.
                self.assertEqual(art.lookup(dict(self.track, motion=False))['art'], front)

    def test_only_verified_album_header(self):
        self.assertEqual(art.album_motion(self.page(),self.candidate),self.base+'master.m3u8')
        self.assertEqual(art.album_motion(self.page(),dict(self.candidate,collectionId=124)),'')
        self.assertEqual(art.album_motion(self.page(),dict(self.candidate,artistName='Other')),'')
        self.assertEqual(art.album_motion(b'<html>No motion</html>',self.candidate),'')

    def test_urls_and_redirects(self):
        for url in ['http://music.apple.com/x','https://music.apple.com.evil/x','https://127.0.0.1/x','file:///tmp/x','https://user@music.apple.com/x','https://music.apple.com:444/x']:
            with self.assertRaises(ValueError): art.safe_url(url)
            with self.assertRaises(ValueError): art.Redirects().redirect_request(None,None,302,'',{},url)

    def test_variant_selection(self):
        raw=b'''#EXTM3U
#EXT-X-I-FRAME-STREAM-INF:BANDWIDTH=10,URI="iframe.m3u8"
#EXT-X-STREAM-INF:BANDWIDTH=800000,CODECS="avc1.64001F",RESOLUTION=486x486,FRAME-RATE=30,VIDEO-RANGE=SDR
small.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=900000,CODECS="hvc1.2.4",RESOLUTION=512x512,VIDEO-RANGE=PQ
hdr.m3u8
#EXT-X-STREAM-INF:BANDWIDTH=900000,CODECS="avc1.64001F",RESOLUTION=1920x1920
large.m3u8
'''
        self.assertEqual(art.variant_url(raw,self.base),self.base+'small.m3u8')
        self.assertEqual(art.variant_url(b'#EXTM3U',self.base),'')

    def manifest(self):
        return b'#EXTM3U\n#EXT-X-MAP:URI="cover.mp4",BYTERANGE="100@0"\n#EXTINF:2,\n#EXT-X-BYTERANGE:1000@100\ncover.mp4\n#EXT-X-ENDLIST\n'

    def test_manifest_boundaries(self):
        self.assertEqual(art.movie_url(self.manifest(),self.base),self.base+'cover.mp4')
        for raw in [self.manifest().replace(b'#EXT-X-ENDLIST',b''),self.manifest().replace(b'2,',b'61,'),
                    self.manifest()+b'#EXT-X-KEY:METHOD=AES-128,URI="key"',self.manifest()+b'other.mp4\n',
                    self.manifest().replace(b'cover.mp4',b'https://localhost/private.mp4'),self.manifest().replace(b'2,',b'nan,')]:
            with self.assertRaises(ValueError): art.movie_url(raw,self.base)

    def test_positive_cache_shared_album_and_recovery(self):
        video=self.root/'test.mp4'
        subprocess.run(['ffmpeg','-nostdin','-v','error','-f','lavfi','-i','testsrc2=size=128x128:rate=10:duration=0.5','-threads','1','-c:v','libx264',str(video)],capture_output=True,check=True)
        calls=[]
        def fetch(url,limit=0):
            calls.append(url)
            if '/search?' in url:return json.dumps(dict(results=[self.candidate])).encode()
            if 'music.apple.com/' in url:return self.page()
            if url.endswith('master.m3u8'):return b'#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=10000,CODECS="avc1.64001F",RESOLUTION=128x128\nvariant.m3u8\n'
            if url.endswith('variant.m3u8'):return self.manifest()
            return video.read_bytes()
        with patch.object(art,'fetch',side_effect=fetch):
            first=art.lookup(self.track);self.assertEqual(first['status'],'ready');self.assertEqual(len(calls),5)
            self.assertEqual(first['art'],self.cover)
            self.assertEqual(art.lookup(self.track),first);self.assertEqual(len(calls),5)
            self.track['seconds']=201
            self.assertEqual(art.lookup(self.track),first);self.assertEqual(len(calls),6)
            (self.root/'cache/123.mp4').unlink()
            self.assertEqual(art.lookup(self.track),first);self.assertEqual(len(calls),11)
            path=self.root/'cache/123.mp4';path.write_bytes(path.read_bytes()[:20])
            self.assertEqual(art.lookup(self.track),first);self.assertEqual(len(calls),16)
            self.track['seconds']=202;path.write_bytes(path.read_bytes()[:20])
            self.assertEqual(art.lookup(self.track),first);self.assertEqual(len(calls),21)
        art.validate_movie(self.root/'cache/123.mp4')
        video.write_bytes(b'not a video')
        with self.assertRaises(ValueError):art.validate_movie(video)

    def test_negative_cache_and_failure_backoff(self):
        # Both providers must answer from fixtures. Leaving the archive side
        # live turned a negative-cache test into a network-dependent retry.
        with patch.object(art,'fetch',return_value=b'{"results":[]}') as fetch, \
             patch.object(art,'fetch_cover',return_value=b'{"recordings":[]}') as covers:
            self.assertEqual(art.lookup(self.track)['status'],'unavailable');art.lookup(self.track)
            self.assertEqual(fetch.call_count,2)
            self.assertEqual(covers.call_count,1)
        self.track['title']='Another song'
        with patch.object(art,'fetch',side_effect=OSError('offline')) as fetch:
            first=art.lookup(self.track);self.assertEqual(first['status'],'retry')
            self.assertEqual(art.lookup(self.track)['status'],'retry');self.assertEqual(fetch.call_count,1)
            self.track['title']='Third song';art.lookup(self.track)
            self.assertEqual(fetch.call_count,2)
        self.assertFalse(list((self.root/'cache').glob('*.mp4')))

    def test_single_uses_original_album_not_compilation(self):
        self.track['album']='A Song'
        compilation=dict(self.candidate,collectionId=124,collectionName='Greatest Hits')
        deluxe=dict(self.candidate,collectionId=125,collectionName='An Album (Deluxe)')
        def collection(item,year):return dict(item,wrapperType='collection',trackCount=12,releaseDate=f'{year}-01-01T00:00:00Z')
        albums=[collection(compilation,2023),collection(deluxe,2020),collection(self.candidate,2020)]
        with patch.object(art,'fetch',return_value=json.dumps(dict(results=albums)).encode()):
            found=art.resolve_candidates(self.track,[compilation,deluxe,self.candidate])
            self.assertEqual([x['collectionId'] for x in found],[123,125])
            self.track['album']=''
            self.assertEqual(art.resolve_candidates(self.track,[compilation,deluxe,self.candidate]),found)
            self.track['seconds']=0
            self.assertEqual(art.resolve_candidates(self.track,[compilation,self.candidate]),[])
        self.track['seconds']=200
        albums[0]['releaseDate']='2020-01-01T00:00:00Z'
        with patch.object(art,'fetch',return_value=json.dumps(dict(results=albums)).encode()):
            self.assertEqual(art.resolve_candidates(self.track,[compilation,self.candidate]),[])

    def test_album_tracklist_fallback_including_long_albums(self):
        other=dict(self.candidate,trackName='Another song')
        with patch.object(art,'fetch',return_value=json.dumps(dict(results=[other]*100+[self.candidate])).encode()) as fetch:
            self.assertEqual(art.resolve_candidates(self.track,[other]),[self.candidate])
            self.assertIn('/lookup?',fetch.call_args.args[0])
        with patch.object(art,'fetch',return_value=json.dumps(dict(results=[dict(self.candidate,artistName='Impostor')])).encode()):
            self.assertEqual(art.resolve_candidates(self.track,[other]),[])

    def test_album_search_fallback_and_wrong_album_rejection(self):
        with patch.object(art,'fetch',side_effect=[json.dumps(dict(results=[self.candidate])).encode(),json.dumps(dict(results=[self.candidate])).encode()]) as fetch:
            self.assertEqual(art.resolve_candidates(self.track,[]),[self.candidate])
            self.assertEqual(fetch.call_count,2)
        with patch.object(art,'fetch',return_value=json.dumps(dict(results=[dict(self.candidate,collectionName='Another album')])).encode()):
            self.assertEqual(art.resolve_candidates(self.track,[]),[])

    def test_rate_limit_cooldown_and_expiry(self):
        from urllib.error import HTTPError
        error=HTTPError(self.base,429,'limited',{'Retry-After':'120'},None);self.addCleanup(error.close)
        with patch.object(art,'time') as clock,patch.object(art,'fetch',side_effect=error) as fetch:
            clock.time.return_value=1000
            self.assertEqual(art.lookup(self.track),dict(status='retry',retryAfter=120))
            self.track['title']='Second track';clock.time.return_value=1000.2
            self.assertEqual(art.lookup(self.track),dict(status='retry',retryAfter=120))
            self.assertEqual(fetch.call_count,1)
            clock.time.return_value=1121
            art.lookup(self.track);self.assertEqual(fetch.call_count,2)

    def test_transient_retry_recovers_without_poisoning_cache(self):
        with patch.object(art,'time') as clock:
            clock.time.return_value=1000
            with patch.object(art,'fetch',side_effect=OSError('offline')) as fetch:
                self.assertEqual(art.lookup(self.track)['status'],'retry')
                clock.time.return_value=1000.2
                self.assertEqual(art.lookup(self.track)['retryAfter'],30)
                self.assertEqual(fetch.call_count,1)
            clock.time.return_value=1031
            with patch.object(art,'fetch',return_value=b'{"results":[]}') as fetch, \
                 patch.object(art,'fetch_cover',return_value=b'{"recordings":[]}'):
                self.assertEqual(art.lookup(self.track)['status'],'unavailable')
                self.assertGreater(fetch.call_count,0)
        self.assertFalse((self.root/'cache/retry-after-v2').exists())

    def test_old_negative_cache_does_not_hide_new_matches(self):
        import hashlib
        cache=self.root/'cache';cache.mkdir()
        key=hashlib.sha256(json.dumps([self.track.get(k,'') for k in ('title','artist','album','seconds')]).encode()).hexdigest()
        (cache/(key+'.json')).write_text(json.dumps(dict(expires=99999999999)))
        (cache/'retry-after').write_text('99999999999')
        with patch.object(art,'fetch',return_value=b'{"results":[]}') as fetch:
            art.lookup(self.track);self.assertGreater(fetch.call_count,0)

    def test_unsupported_candidate_does_not_block_other_albums(self):
        (self.root/'cache').mkdir();(self.root/'cache/124.mp4').write_bytes(b'\0\0\0\x18ftypisom')
        with patch.object(art,'validate_movie'),patch.object(art,'resolve_candidates',return_value=[self.candidate,dict(self.candidate,collectionId=124)]),patch.object(art,'fetch',side_effect=[b'{"results":[]}',ValueError('unsupported cover')]):
            self.assertEqual(art.lookup(self.track)['status'],'ready')
        self.assertFalse((self.root/'cache/retry-after-v2').exists())

    def test_transport_size_caps(self):
        from unittest.mock import MagicMock
        opener=MagicMock(); response=opener.open.return_value.__enter__.return_value
        response.headers={'Content-Length':'999'}
        with patch.object(art,'build_opener',return_value=opener):
            with self.assertRaises(ValueError):art.fetch(self.base,10)
            response.headers={};response.read.return_value=b'x'*11
            with self.assertRaises(ValueError):art.fetch(self.base,10)
            response.read.return_value=b'ok'
            self.assertEqual(art.fetch(self.base,10),b'ok')

    def test_cache_disk_and_metadata_bounds(self):
        cache=self.root/'cache';cache.mkdir()
        for i in range(5):(cache/f'{i}.mp4').write_bytes(b'x'*100)
        for i in range(260):(cache/f'{i}.json').write_text('{}')
        with patch.object(art,'CACHE_LIMIT',250):art.prune(cache)
        self.assertEqual(len(list(cache.glob('*.mp4'))),2)
        self.assertEqual(len(list(cache.glob('*.json'))),256)

if __name__=='__main__':unittest.main()
