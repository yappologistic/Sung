"""Catalog contracts: coverage, placeholders and file filters."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


class TranslationCatalogTests(unittest.TestCase):
    def test_russian_catalog_covers_marked_qml(self):
        catalog = {}
        for context in ET.parse(ROOT / 'translations/sung_ru.ts').getroot().findall('context'):
            for message in context.findall('message'):
                if message.find('translation').get('type') in ('vanished', 'obsolete'):
                    continue
                key = (context.findtext('name'), message.findtext('source'))
                self.assertNotIn(key, catalog, f'duplicate translation: {key}')
                catalog[key] = message.find('translation')
        for path in (ROOT / 'qml').glob('*.qml'):
            text = path.read_text()
            strings = [(path.stem, match[1]) for match in re.findall(r'qsTr\(\s*(["\'])(.*?)\1', text)]
            strings += [(match[1], match[3]) for match in re.findall(r'qsTranslate\(\s*(["\'])(.*?)\1\s*,\s*(["\'])(.*?)\3', text)]
            for key in strings:
                self.assertIn(key, catalog, f'{path.name}: {key}')
                self.assertNotEqual(catalog[key].get('type'), 'unfinished', key)
                self.assertTrue(''.join(catalog[key].itertext()).strip(), key)
        for (_, source), translation in catalog.items():
            forms = translation.findall('numerusform') or [translation]
            for form in forms:
                translated = ''.join(form.itertext())
                self.assertEqual(sorted(re.findall(r'%(?:n|\d+)', source)),
                                 sorted(re.findall(r'%(?:n|\d+)', translated)), source)
                self.assertEqual(re.findall(r'\*\.\w+', source), re.findall(r'\*\.\w+', translated), source)

    def test_settings_visible_literals_are_marked(self):
        text = (ROOT / 'qml/Main.qml').read_text()
        settings = text.split('id: settingsDialog;', 1)[1].split('id: trimDialog;', 1)[0]
        literals = re.findall(r'\b(?:text|label|accessibleName)\s*:\s*"([A-Za-z][^"\n]*)"', settings)
        self.assertEqual([value for value in literals if value not in ('English', 'YouTube', 'Sung ')], [])

    def test_normal_launcher_preserves_user_environment(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for folder in ('scripts', 'build', 'runtime/bin'):
                (root / folder).mkdir(parents=True, exist_ok=True)
            script = root / 'scripts/run.sh'
            shutil.copy2(ROOT / 'scripts/run.sh', script)
            (root / 'runtime/bin/python').symlink_to('/usr/bin/python3')
            binary = root / 'build/sung'
            binary.write_text('#!/usr/bin/python3\nimport json,os,sys\nprint(json.dumps({"env":dict(os.environ),"args":sys.argv[1:]}))\n')
            binary.chmod(0o700)
            profile = root / 'user-profile'
            config = profile / 'config/Sung/sung.conf'
            config.parent.mkdir(parents=True)
            config.write_text('[General]\ninterfaceLanguage=ru\ntheme=system\n')
            env = dict(os.environ, XDG_CONFIG_HOME=str(profile / 'config'),
                       XDG_DATA_HOME=str(root / 'desktop'), XDG_CACHE_HOME=str(root / 'cache'),
                       SUNG_NOCTALIA_COLORS=str(root / 'desktop/noctalia.colors'))
            result = json.loads(subprocess.check_output(['bash', str(script), '--mini'], env=env, text=True))
            self.assertEqual(result['args'], ['--mini'])
            self.assertEqual(result['env']['SUNG_HELPER'], str(root / 'helper/catalog.py'))
            self.assertEqual(result['env']['SUNG_PYTHON'], str(root / 'runtime/bin/python'))
            for variable in ('XDG_DATA_HOME', 'XDG_CACHE_HOME', 'SUNG_NOCTALIA_COLORS'):
                self.assertEqual(result['env'][variable], env[variable])
            self.assertEqual(result['env']['XDG_CONFIG_HOME'], str(profile / 'config'))
            self.assertEqual(config.read_text(), '[General]\ninterfaceLanguage=ru\ntheme=system\n')


if __name__ == '__main__':
    unittest.main()
