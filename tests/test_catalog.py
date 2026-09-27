import importlib.util
import pathlib
import unittest
from unittest.mock import patch, MagicMock

spec=importlib.util.spec_from_file_location('catalog',pathlib.Path(__file__).parents[1]/'helper/catalog.py')
catalog=importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)

class CatalogTests(unittest.TestCase):
    def test_lyrics_fallback_matching_and_limits(self):
        from unittest.mock import patch, MagicMock
        import json, tempfile
        from urllib.error import HTTPError
        req=dict(title='Song',artist='Artist',seconds=120)
        data=dict(trackName='Song',artistName='Artist',duration=120,syncedLyrics='[00:01] Hello')
        response=MagicMock();response.__enter__.return_value=response
        with patch('urllib.request.urlopen',return_value=response) as get:
            response.read.return_value=json.dumps(data).encode()
            self.assertEqual(catalog.lyric_fallback(req)['source'],'LRCLIB')
            self.assertEqual(get.call_args.kwargs['timeout'],8)
            for field,value in [('trackName','Song (Live)'),('artistName','Other'),('duration',124)]:
                response.read.return_value=json.dumps({**data,field:value}).encode()
                self.assertIsNone(catalog.lyric_fallback(req))
            response.read.return_value=b'x'*1048577
            self.assertIsNone(catalog.lyric_fallback(req))
        with tempfile.TemporaryDirectory() as cache:
            req['lyricCache']=cache
            with patch('urllib.request.urlopen',side_effect=HTTPError('',429,'limit',{'Retry-After':'600'},None)) as get:
                self.assertIsNone(catalog.lyric_fallback(req))
                self.assertIsNone(catalog.lyric_fallback(req))
                self.assertEqual(get.call_count,1)

    def test_secondary_lyrics_preserve_primary_and_recover_failure(self):
        from unittest.mock import patch, MagicMock
        api=MagicMock();api.get_watch_playlist.return_value={'lyrics':'source'}
        api.get_lyrics.return_value={'lyrics':'Primary plain'}
        req={'op':'lyrics','id':'abcdefghijk','fallback':True}
        with patch.dict('sys.modules',{'ytmusicapi':MagicMock(YTMusic=MagicMock(return_value=api))}):
            with patch.object(catalog,'lyric_fallback',side_effect=OSError('offline')):
                self.assertEqual(catalog.run(req)['lyrics'],'Primary plain')
            api.get_lyrics.return_value={'lyrics':[{'text':'Timed','start_time':1000}]}
            with patch.object(catalog,'lyric_fallback') as fallback:
                self.assertTrue(catalog.run(req)['lines']);fallback.assert_not_called()
            api.get_watch_playlist.side_effect=OSError('primary unavailable')
            with patch.object(catalog,'lyric_fallback',return_value={'source':'LRCLIB','lrc':'[00:01] Fallback'}):
                self.assertEqual(catalog.run(req)['source'],'LRCLIB')

    def test_local_metadata_embedded_art_and_missing_files(self):
        import subprocess, tempfile
        from urllib.parse import urlparse, unquote
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory);cover=root/'embedded-source.jpg';song=root/'Café #1.flac'
            def ff(*args): subprocess.run(['ffmpeg','-nostdin','-v','error',*args],check=True,capture_output=True,timeout=10)
            ff('-f','lavfi','-i','color=c=blue:s=32x32','-frames:v','1','-threads','1',str(cover))
            ff('-f','lavfi','-i','anullsrc=r=44100:cl=mono','-i',str(cover),'-map','0:a','-map','1:v','-c:a','flac','-c:v','copy','-disposition:v','attached_pic','-t','1','-metadata','title=Tagged title','-metadata','artist=Artist',str(song))
            alias=root/'alias.flac';alias.symlink_to(song)
            result=catalog.local_files({'files':[str(song),str(alias),str(root/'missing.mp3')],'artDirectory':str(root/'art')})
            self.assertEqual(len(result['items']),2)
            self.assertEqual(result['items'][0]['id'],result['items'][1]['id'])
            self.assertEqual(result['items'][0]['title'],'Tagged title')
            self.assertEqual(result['items'][0]['videoId'],'')
            self.assertTrue(pathlib.Path(unquote(urlparse(result['items'][0]['art']).path)).is_file())
            self.assertEqual(result['failed'],['missing.mp3'])

    def test_folder_scan_nested_dedup_and_incremental(self):
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory); nested=root/'Album';nested.mkdir()
            song=nested/'Café.FLAC';song.write_bytes(b'fixture')
            (root/'ignore.txt').write_text('not audio')
            (root/'alias.flac').symlink_to(song)
            (nested/'loop').symlink_to(root, target_is_directory=True)
            result=catalog.scan_music_folders({'folders':[str(root),str(nested)]})
            self.assertEqual(result['files'],[str(song)])
            self.assertFalse(result['limited'])
            st=song.stat();known={str(song):f'{st.st_mtime_ns}:{st.st_size}'}
            self.assertEqual(catalog.scan_music_folders({'folders':[str(root)],'known':known})['files'],[])
            song.write_bytes(b'changed fixture')
            self.assertEqual(catalog.scan_music_folders({'folders':[str(root)],'known':known})['files'],[str(song)])
            self.assertEqual(catalog.scan_music_folders({'folders':[str(root/'absent')]})['failed'],1)

    def test_cleanup_exact_identity_and_missing_files(self):
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory);song=root/'Song.flac';song.write_bytes(b'audio')
            alias=root/'Alias.flac';alias.symlink_to(song)
            rows=[dict(id='a',videoId='a',title='Song'),dict(id='b',videoId='b',title='Song'),dict(id='c',videoId='a'),dict(localPath=str(song)),dict(localPath=str(alias)),dict(localPath=str(root/'Gone.mp3'))]
            issues=catalog.playlist_cleanup({'rows':rows})['issues']
            self.assertEqual(issues,[dict(index=2,duplicate=True,missing=False),dict(index=4,duplicate=True,missing=False),dict(index=5,duplicate=False,missing=True)])
            self.assertTrue(song.exists())

    def test_normalization(self):
        t=catalog.normalize({'title':'Song','videoId':'12345678901','artists':[{'name':'Artist','id':'UC1'}], 'album':{'name':'Album','id':'MPRE1'},'duration':'3:04','isExplicit':True},'song')
        self.assertEqual((t['kind'],t['artist'],t['albumId'],t['duration']),('song','Artist','MPRE1','3:04'))
        self.assertTrue(t['explicit'])
    def test_parent_art_and_album(self):
        p={'type':'album','title':'Record','browseId':'MPRE1','artists':[{'name':'A'}],'thumbnails':[{'url':'https://example.com/a.jpg'}]}
        t=catalog.normalize({'videoId':'12345678901'},'song',p)
        self.assertEqual(t['art'],'https://example.com/a.jpg')
        self.assertEqual(t['album'],'Record')
    def test_watch_playlist_track_keeps_its_cover(self):
        # ytmusicapi's parse_watch_track writes `thumbnail`, singular, where
        # search and browse results write `thumbnails`. Radio and autoplay are
        # both built from watch playlists, so reading only the plural left
        # every song they queued with no cover at all.
        track={'title':'Somewhere I Belong','videoId':'12345678901',
               'artists':[{'name':'Linkin Park','id':'UC1'}],
               'thumbnail':[{'url':'https://lh3.googleusercontent.com/a=w60-h60-l90-rj'}]}
        self.assertTrue(catalog.artwork(track))
        self.assertTrue(catalog.normalize(track,'song')['art'])
        # The larger size is still asked for, the same as any other cover.
        self.assertIn('w544-h544',catalog.normalize(track,'song')['art'])
        # And the plural still wins where both are present, because that is
        # the one search results carry.
        both=dict(track,thumbnails=[{'url':'https://example.com/plural.jpg'}])
        self.assertEqual(catalog.artwork(both),'https://example.com/plural.jpg')

    def test_unavailable_and_empty(self):
        self.assertEqual(catalog.clean([{},None,{'title':'No ID'}]),[])
        self.assertFalse(catalog.normalize({'videoId':'12345678901','isAvailable':False})['available'])
    def test_artist_search_name(self):
        t=catalog.normalize({'artist':'Nujabes','browseId':'UC1','resultType':'artist'},'artist')
        self.assertEqual(t['title'],'Nujabes')
    def test_timed_lyrics(self):
        from dataclasses import make_dataclass
        Line=make_dataclass('Line',[('text',str),('start_time',int),('end_time',int),('id',int)])
        data=catalog.normalize_lyrics({'lyrics':[Line('Second',3000,4000,2),Line('First',1000,2000,1),{'text':'invalid','start_time':-1}]})
        self.assertEqual([l['start'] for l in data['lines']],[1000,3000])
        self.assertEqual(data['lyrics'],'First\nSecond')
        self.assertEqual(catalog.normalize_lyrics({'lyrics':'Plain'})['lines'],[])
        self.assertEqual(catalog.normalize_lyrics(None),{'lyrics':'','lines':[]})
    def test_streaming_quality_formats(self):
        standard=catalog.audio_format('standard')
        saver=catalog.audio_format('saver')
        # Standard takes Opus in WebM first, which is what YouTube serves best.
        self.assertTrue(standard.startswith('bestaudio[ext=webm]/'))
        self.assertNotIn('abr', standard)
        # Data saver caps the bitrate on every choice, and still ends somewhere.
        self.assertTrue(all('[abr<=80]' in c for c in saver.split('/')[:-1]))
        self.assertTrue(saver.endswith('/worstaudio'))
        # A retry asks a different container, not the same stream again.
        self.assertTrue(catalog.audio_format('standard',True).startswith('bestaudio[ext=m4a]/'))
        self.assertTrue(catalog.audio_format('saver',True).startswith('bestaudio[ext=m4a][abr<=80]/'))
        # Anything that is not a quality this build knows streams normally.
        for unknown in ('','lossless',None):
            self.assertEqual(catalog.audio_format(unknown),standard)

    def test_streaming_quality_reaches_ytdlp(self):
        from unittest.mock import patch, MagicMock
        for quality,expected in [('saver',catalog.audio_format('saver')),('standard',catalog.audio_format('standard'))]:
            downloader=MagicMock();downloader.__enter__.return_value=downloader
            downloader.extract_info.return_value={'url':'https://example.invalid/a','duration':1}
            with patch.dict('sys.modules',{'yt_dlp':MagicMock(YoutubeDL=MagicMock(return_value=downloader))}) as mods:
                catalog.run({'op':'resolve','id':'abcdefghijk','quality':quality})
                self.assertEqual(mods['yt_dlp'].YoutubeDL.call_args[0][0]['format'],expected)

    def test_image_size(self):
        self.assertEqual(catalog.artwork({'thumbnails':[{'url':'https://yt3.googleusercontent.com/a=w60-h60-l90-rj'}]}),'https://yt3.googleusercontent.com/a=w544-h544-l90-rj')

    def test_parse_credentials_formats(self):
        # 1. Innertube token format
        token = '***INNERTUBE COOKIE*** = SAPISID=sec123; SID=sid123\n***AUTH USER*** = 2'
        cmap, sapisid, user, _, _ = catalog.parse_credentials(token)
        self.assertEqual(sapisid, 'sec123')
        self.assertEqual(user, '2')
        self.assertEqual(cmap.get('SID'), 'sid123')

        # 2. Netscape cookies format
        netscape = '# Netscape HTTP Cookie File\n.youtube.com\tTRUE\t/\tTRUE\t2147483647\tSAPISID\tnet123\n'
        cmap, sapisid, user, _, _ = catalog.parse_credentials(netscape)
        self.assertEqual(sapisid, 'net123')

        # 3. HTTP headers format
        headers = 'Cookie: SAPISID=hdr123; HSID=h123\r\nX-Goog-AuthUser: 1'
        cmap, sapisid, user, _, _ = catalog.parse_credentials(headers)
        self.assertEqual(sapisid, 'hdr123')
        self.assertEqual(user, '1')

        # 4. Raw cookie string
        raw = 'SAPISID=raw123; OTHER=oth'
        cmap, sapisid, user, _, _ = catalog.parse_credentials(raw)
        self.assertEqual(sapisid, 'raw123')

        # 5. Missing SAPISID
        cmap, sapisid, user, _, _ = catalog.parse_credentials('RANDOM=val')
        self.assertIsNone(sapisid)

    def test_yt_account_and_sync_ops(self):
        from unittest.mock import patch, MagicMock
        import tempfile
        api = MagicMock()
        api.get_account_info.return_value = {
            'accountName': 'Test Singer',
            'channelHandle': '@testsinger',
            'accountPhotoUrl': 'https://example.com/photo.jpg',
        }
        api.get_liked_songs.return_value = {
            'tracks': [{
                'videoId': 'vid12345678',
                'title': 'Liked Song',
                'artists': [{'name': 'Liked Artist', 'id': 'art1'}],
                'album': {'name': 'Liked Album', 'id': 'alb1'},
                'duration': '2:30',
                'duration_seconds': 150,
            }]
        }
        api.get_library_playlists.return_value = [
            {'playlistId': 'LM', 'title': 'Liked Music'},
            {'playlistId': 'PLremote123', 'title': 'My Remote Playlist'}
        ]
        api.get_playlist.return_value = {
            'title': 'My Remote Playlist',
            'tracks': [{
                'videoId': 'vid87654321',
                'title': 'Playlist Track',
                'artists': [{'name': 'Artist 2'}],
            }]
        }

        with patch.dict('sys.modules', {'ytmusicapi': MagicMock(YTMusic=MagicMock(return_value=api))}):
            with tempfile.TemporaryDirectory() as td:
                res = catalog.run({
                    'op': 'yt-account',
                    'credentials': 'SAPISID=valid_sapisid; SID=valid_sid',
                    'dataPath': td
                })
                self.assertTrue(res['ok'])
                self.assertEqual(res['name'], 'Test Singer')
                self.assertEqual(res['handle'], '@testsinger')
                self.assertEqual(res['photo'], 'https://example.com/photo.jpg')
                self.assertTrue(pathlib.Path(res['cookieFile']).is_file())
                self.assertTrue(pathlib.Path(res['authFile']).is_file())

                # Test sync
                sync_res = catalog.run({
                    'op': 'yt-sync',
                    'cookies': res['cookieFile'],
                    'dataPath': td
                })
                self.assertTrue(sync_res['ok'])
                self.assertEqual(len(sync_res['liked']), 1)
                self.assertEqual(sync_res['liked'][0]['id'], 'vid12345678')
                self.assertEqual(len(sync_res['playlists']), 1)
                self.assertEqual(sync_res['playlists'][0]['id'], 'PLremote123')
                self.assertEqual(len(sync_res['playlists'][0]['tracks']), 1)

                # Test like
                like_res = catalog.run({
                    'op': 'yt-like',
                    'id': 'vid12345678',
                    'liked': True,
                    'cookies': res['cookieFile'],
                })
                self.assertTrue(like_res['ok'])
                api.rate_song.assert_called_with('vid12345678', 'LIKE')

    def test_browser_login_flow(self):
        import tempfile
        with patch.object(catalog, 'extract_browser_cookies', return_value={'ok': True, 'cookies': 'SAPISID=sec1; SID=s1', 'browser': 'testbrowser'}):
            api = MagicMock()
            api.get_account_info.return_value = {'accountName': 'Browser User', 'channelHandle': '@browser', 'accountPhotoUrl': ''}
            with patch.dict('sys.modules', {'ytmusicapi': MagicMock(YTMusic=MagicMock(return_value=api))}):
                with tempfile.TemporaryDirectory() as td:
                    res = catalog.run({'op': 'yt-browser-login', 'mode': 'browser', 'dataPath': td})
                    self.assertTrue(res['ok'])
                    self.assertEqual(res['name'], 'Browser User')
                    self.assertEqual(res['browser'], 'testbrowser')

    def test_rebuild_auth_headers_fresh_timestamp(self):
        import tempfile, stat, json
        with tempfile.TemporaryDirectory() as td:
            auth_file = pathlib.Path(td) / 'auth.json'
            # Write auth.json with an old timestamp and custom authuser
            stale_headers = {
                'Cookie': 'SAPISID=sec123; SID=sid123',
                'Authorization': 'SAPISIDHASH 1000000000_stalehash',
                'X-Goog-AuthUser': '3',
            }
            auth_file.write_text(json.dumps(stale_headers))

            headers = catalog.build_auth_headers(str(auth_file))
            self.assertIsNotNone(headers)
            self.assertEqual(headers.get('X-Goog-AuthUser'), '3')
            self.assertTrue(headers.get('Authorization', '').startswith('SAPISIDHASH '))
            # Verify the timestamp is freshly generated and not the stale 1000000000
            ts_str = headers['Authorization'].split()[1].split('_')[0]
            self.assertGreater(int(ts_str), 1700000000)

    def test_credential_file_permissions(self):
        import tempfile, stat
        api = MagicMock()
        api.get_account_info.return_value = {'accountName': 'Perm User'}
        with patch.dict('sys.modules', {'ytmusicapi': MagicMock(YTMusic=MagicMock(return_value=api))}):
            with tempfile.TemporaryDirectory() as td:
                res = catalog.run({
                    'op': 'yt-account',
                    'credentials': 'SAPISID=sec123; SID=sid123',
                    'dataPath': td
                })
                self.assertTrue(res['ok'])
                auth_mode = stat.S_IMODE(pathlib.Path(res['authFile']).stat().st_mode)
                cookie_mode = stat.S_IMODE(pathlib.Path(res['cookieFile']).stat().st_mode)
                dp_mode = stat.S_IMODE(pathlib.Path(td).stat().st_mode)
                self.assertEqual(auth_mode, 0o600)
                self.assertEqual(cookie_mode, 0o600)
                self.assertEqual(dp_mode, 0o700)

    def test_get_ytmusic_require_auth_errors(self):
        # 1. No auth when required raises ValueError
        with self.assertRaises(ValueError) as ctx:
            catalog.get_ytmusic({}, require_auth=True)
        self.assertIn('Sign in to YouTube Music', str(ctx.exception))

        # 2. Invalid auth raises clear session error instead of silent fallback
        failing_ytmusic = MagicMock(side_effect=Exception('Invalid credentials'))
        with patch.dict('sys.modules', {'ytmusicapi': MagicMock(YTMusic=failing_ytmusic)}):
            with self.assertRaises(ValueError) as ctx:
                catalog.get_ytmusic({'auth': 'SAPISID=sec123; SID=sid123'}, require_auth=True)
    def test_yt_account_validation_failure_preserves_existing_credentials(self):
        import tempfile
        api = MagicMock()
        api.get_account_info.side_effect = Exception('Session expired')
        api.get_library_playlists.side_effect = Exception('Session expired')
        with patch.dict('sys.modules', {'ytmusicapi': MagicMock(YTMusic=MagicMock(return_value=api))}):
            with tempfile.TemporaryDirectory() as td:
                dp = pathlib.Path(td)
                auth_file = dp / 'auth.json'
                cookie_file = dp / 'cookies.txt'
                auth_file.write_text('{"original": "auth"}')
                cookie_file.write_text('original_cookies')

                with self.assertRaises(ValueError) as ctx:
                    catalog.run({
                        'op': 'yt-account',
                        'credentials': 'SAPISID=sec123; SID=sid123',
                        'dataPath': td
                    })
                self.assertIn('invalid or expired', str(ctx.exception))
                # Verify existing files were NOT overwritten
                self.assertEqual(auth_file.read_text(), '{"original": "auth"}')
                self.assertEqual(cookie_file.read_text(), 'original_cookies')

    def test_yt_sync_completeness_flag(self):
        import tempfile
        api = MagicMock()
        api.get_liked_songs.return_value = {'tracks': []}
        api.get_library_playlists.return_value = [
            {'playlistId': 'PLpartial', 'title': 'Partial Playlist'},
            {'playlistId': 'PLfull', 'title': 'Full Playlist'},
            {'playlistId': 'PLempty', 'title': 'Empty Playlist'},
            {'playlistId': 'PLunknown_full', 'title': 'Unknown Full Playlist'},
            {'playlistId': 'PLunknown_hit_limit', 'title': 'Unknown Hit Limit Playlist'},
        ]
        def mock_get_playlist(pid, **kwargs):
            if pid == 'PLpartial':
                return {
                    'title': 'Partial Playlist',
                    'trackCount': 10,
                    'tracks': [{'videoId': 'vid1', 'title': 'Song 1'}]
                }
            elif pid == 'PLfull':
                return {
                    'title': 'Full Playlist',
                    'trackCount': 2,
                    'tracks': [{'videoId': 'vid1', 'title': 'Song 1'}, {'videoId': 'vid2', 'title': 'Song 2'}]
                }
            elif pid == 'PLempty':
                return {
                    'title': 'Empty Playlist',
                    'trackCount': 0,
                    'tracks': []
                }
            elif pid == 'PLunknown_full':
                return {
                    'title': 'Unknown Full Playlist',
                    'tracks': [{'videoId': 'vid1', 'title': 'Song 1'}, {'videoId': 'vid2', 'title': 'Song 2'}]
                }
            elif pid == 'PLunknown_hit_limit':
                return {
                    'title': 'Unknown Hit Limit Playlist',
                    'tracks': [{'videoId': f'vid{i}', 'title': f'Song {i}'} for i in range(5)]
                }
            return {'title': 'Unknown', 'tracks': []}

        api.get_playlist.side_effect = mock_get_playlist

        with patch.dict('sys.modules', {'ytmusicapi': MagicMock(YTMusic=MagicMock(return_value=api))}):
            with tempfile.TemporaryDirectory() as td:
                auth_file = pathlib.Path(td) / 'auth.json'
                auth_file.write_text('{"Cookie": "SAPISID=sapisid"}')
                res = catalog.run({
                    'op': 'yt-sync',
                    'auth': str(auth_file),
                    'dataPath': td,
                    'playlistLimit': 5,
                })
                self.assertTrue(res['ok'])
                pls = {p['id']: p for p in res['playlists']}
                self.assertFalse(pls['PLpartial']['complete'])
                self.assertTrue(pls['PLfull']['complete'])
                self.assertTrue(pls['PLempty']['complete'])
                self.assertTrue(pls['PLunknown_full']['complete'])
                self.assertFalse(pls['PLunknown_hit_limit']['complete'])

if __name__=='__main__':unittest.main()
