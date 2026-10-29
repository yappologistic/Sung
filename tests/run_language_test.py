#!/usr/bin/env python3
"""Run only the language UI stage, using an isolated profile and silent audio."""
import argparse
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=False)
output.chmod(0o700)
env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
           QSG_RENDER_LOOP='basic', QT_QPA_PLATFORMTHEME='generic',
           LANG='en_US.UTF-8', LC_ALL='en_US.UTF-8',
           SUNG_HELPER=str(root / 'tests/catalog_fixture.py'), SUNG_PYTHON='/usr/bin/python3',
           SUNG_TEST_OUTPUT=str(output))
env['SUNG_NOCTALIA_COLORS'] = str(output / 'noctalia.colors')
for variable, name in [('XDG_CONFIG_HOME', 'config'), ('XDG_DATA_HOME', 'data'), ('XDG_CACHE_HOME', 'cache')]:
    env[variable] = str(output / name)
for flag in ['--language-test', '--language-startup-test']:
    with (output / (flag[2:] + '.log')).open('w') as log:
        result = subprocess.run([str(args.binary.resolve()), '--isolated', '--offline', flag],
                                env=env, stdout=log, stderr=subprocess.STDOUT, timeout=90)
    text = (output / (flag[2:] + '.log')).read_text()
    print(text, end='')
    if result.returncode or 'FAIL' in text or 'overrides a member' in text or 'ReferenceError:' in text or 'TypeError:' in text:
        raise SystemExit(result.returncode or 1)
