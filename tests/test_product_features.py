import importlib.util
import pathlib
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('catalog', pathlib.Path(__file__).parents[1] / 'helper/catalog.py')
catalog = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)

class ProductFeatureTests(unittest.TestCase):
    def test_scan_reports_missing_only_with_complete_readable_roots(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            present = root / 'song.wav'
            present.write_bytes(b'fixture')
            missing = str(root / 'gone.wav')
            external = str(root.parent / 'outside.wav')
            result = catalog.scan_music_folders({'folders': [folder], 'known': {missing: 'old', external: 'old'}})
            self.assertEqual(result['missing'], [missing])
            self.assertIn(str(present), result['watchPaths'])
            self.assertIn(folder, result['watchPaths'])
            self.assertNotIn(str(root.parent), result['watchPaths'])
            with patch('os.scandir', side_effect=PermissionError()):
                result = catalog.scan_music_folders({'folders': [folder], 'known': {missing: 'old'}})
            self.assertEqual(result['missing'], [])
            self.assertGreater(result['failed'], 0)

    def test_artwork_picker_rejects_remote_and_unreadable_input(self):
        with tempfile.TemporaryDirectory() as folder:
            for path in ('https://example.invalid/cover.mp4', 'relative.gif', folder + '/missing.mp4'):
                self.assertEqual(catalog.run({'op': 'choose-artwork', 'path': path, 'artDirectory': folder})['motionArt'], '')

    def test_disc_metadata_preserved(self):
        row = catalog.normalize({'videoId': 'fixture', 'discNumber': 2, 'title': 'Second disc'})
        self.assertEqual(row['discNumber'], 2)

    def test_local_album_tags(self):
        import json
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as folder:
            song = pathlib.Path(folder) / 'song.flac'
            song.write_bytes(b'fixture')
            data = {'streams': [{'codec_type': 'audio'}], 'format': {'duration': '120', 'tags': {'ALBUM_ARTIST': 'Various Artists', 'ALBUM': 'Compilation', 'ARTIST': 'Performer', 'TRACK': '3/12', 'DISC': '2/2', 'DATE': '2024-01-01'}}}
            with patch('subprocess.run', return_value=SimpleNamespace(returncode=0, stdout=json.dumps(data).encode())):
                result = catalog.run({'op': 'local-files', 'files': [str(song)]})
            track = result['items'][0]
            self.assertEqual((track['albumArtist'], track['trackNumber'], track['discNumber'], track['year']), ('Various Artists', 3, 2, '2024'))
            self.assertEqual(track['artist'], 'Performer')
        for value in ('', 'bad', '-1', None):
            self.assertEqual(catalog.tag_number(value), 1)
