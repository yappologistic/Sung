#!/usr/bin/env python3
"""Live artwork checks using anonymous song searches and isolated application data."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root/'helper'))
from online_artwork import normal, title_key

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--binary', type=Path, help='Also run the diagnostics build with real audio, muted')
parser.add_argument('--rounds', type=int, choices=(1, 2), default=2)
args = parser.parse_args()
out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False); out.chmod(0o700)
python = os.environ.get('SUNG_PYTHON') or str(root/'runtime/bin/python')
helper = str(root/'helper/catalog.py')


def request(data, timeout=50):
    proc = subprocess.run([python, helper], input=json.dumps(data), text=True, capture_output=True, timeout=timeout)
    result = json.loads(proc.stdout)
    if proc.returncode or not result.get('ok'):
        raise RuntimeError('Catalog helper failed: '+str(result.get('error', proc.returncode)))
    return result


songs = [('The Weeknd', 'Blinding Lights'), ('Dua Lipa', 'Levitating'),
         ('Billie Eilish', 'Happier Than Ever'), ('Taylor Swift', 'Anti-Hero'), ('Bad Bunny', 'Moscow Mule')]
rows, tracks = [], []
for index, (artist, title) in enumerate(songs):
    row = dict(artist=artist, title=title, checks=[])
    try:
        items = request(dict(op='search', query=artist+' '+title, filter='songs', limit=10))['items']
        track = next(i for i in items if normal(i.get('artist', '')) == normal(artist) and title_key(i.get('title', '')) == title_key(title))
        tracks.append(track)
        for attempt in range(args.rounds):
            req = dict(track, op='online-artwork', artworkCache=str(out/f'cache-{index}-{attempt}'), scratch=str(out/f'scratch-{index}-{attempt}'))
            start = time.monotonic(); first = request(req)
            row['checks'].append(dict(kind='cold', passed=first.get('status')=='ready', seconds=round(time.monotonic()-start, 3)))
            for _ in range(3):
                start = time.monotonic(); cached = request(req)
                row['checks'].append(dict(kind='cached-process', passed=cached==first and cached.get('status')=='ready', seconds=round(time.monotonic()-start, 3)))
    except (OSError, ValueError, KeyError, StopIteration, RuntimeError, subprocess.SubprocessError) as error:
        row['error'] = str(error)
    row['passed'] = bool(row['checks']) and not row.get('error') and all(c['passed'] for c in row['checks'])
    rows.append(row); (out/'lookup.json').write_text(json.dumps(rows, indent=2))
    print(('PASS ' if row['passed'] else 'FAIL ')+artist+' — '+title, flush=True)

(out/'tracks.json').write_text(json.dumps(tracks, indent=2))
passed = all(row['passed'] for row in rows)
if args.binary and passed:
    env = os.environ.copy()
    env.update(SUNG_HELPER=helper, SUNG_PYTHON=python, SUNG_TEST_OUTPUT=str(out/'ui'), SUNG_LIVE_ARTWORK_TRACKS=str(out/'tracks.json'))
    for var, folder in [('XDG_CONFIG_HOME', 'config'), ('XDG_DATA_HOME', 'data'), ('XDG_CACHE_HOME', 'cache')]:
        env[var] = str(out/'ui-profile'/folder)
    with (out/'ui.log').open('w') as log:
        result = subprocess.run([str(args.binary.resolve()), '--isolated', '--online-artwork-live-test'], env=env, stdout=log, stderr=subprocess.STDOUT, timeout=900)
    passed = result.returncode == 0
    print('PASS live playback and UI' if passed else 'FAIL live playback and UI', flush=True)
(out/'result.json').write_text(json.dumps(dict(passed=passed, albums=len(rows), rounds=args.rounds, ui=bool(args.binary)), indent=2))
sys.exit(0 if passed else 1)
