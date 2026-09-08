import importlib.util
import pathlib
import unittest

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
    def test_image_size(self):
        self.assertEqual(catalog.artwork({'thumbnails':[{'url':'https://yt3.googleusercontent.com/a=w60-h60-l90-rj'}]}),'https://yt3.googleusercontent.com/a=w544-h544-l90-rj')

if __name__=='__main__':unittest.main()
