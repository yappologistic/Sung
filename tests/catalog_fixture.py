"""Deterministic transport fixture. Never used by the shipped application."""
import json, sys, time
r = json.load(sys.stdin)
op=r.get('op')
def song(i):
    return dict(id=f'{i:011d}', videoId=f'{i:011d}', title=f'Track {i}',kind='song',artist='Test artist',seconds=120,available=True)
data={'ok':True}
if op=='home': data['sections']=[{'title':'Fixture shelf','items':[song(1)]}]
elif op=='search':
    if r['query']=='slow': time.sleep(2)
    if r['query']=='error': data={'ok':False,'error':'ConnectionError: fixture offline'}
    else: data['items']=[song(i) for i in range(r.get('limit',30))] if r['query']!='empty' else []
elif op in ('album','playlist','link'):
    data.update(title='Collection',items=[song(1),song(2)],total=2)
    import os
    if op=='album' and os.environ.get('SUNG_ALBUM_FIXTURE'):
        data.update(artist='Test artist', year='2026', art=os.environ['SUNG_ALBUM_FIXTURE'])
        data['items'][0]['discNumber']=1;data['items'][1]['discNumber']=2
elif op=='artist': data.update(title='Test artist',sections=[{'title':'Songs','items':[song(1)]}])
elif op=='lyrics':
    data['lyrics']='Test lyrics'
    if r.get('id')=='timedlyric1':data['lines']=[{'start':1000,'end':2000,'text':'First'},{'start':3000,'end':4000,'text':'Second'}]
elif op in ('local-files','scan-folders','playlist-cleanup','choose-artwork'):
    import importlib.util
    from pathlib import Path
    spec=importlib.util.spec_from_file_location('catalog',Path(__file__).parents[1]/'helper/catalog.py');catalog=importlib.util.module_from_spec(spec);spec.loader.exec_module(catalog)
    data.update(catalog.run(r))
elif op=='online-artwork':
    import os
    from pathlib import Path
    if r.get('title')=='slow motion':time.sleep(2)
    marker=Path(r['artworkCache'])/'retry-fixture'
    if r.get('title')=='retry motion' and not marker.exists():
        marker.parent.mkdir(parents=True,exist_ok=True);marker.write_text(str(time.time()+1));data.update(status='retry',retryAfter=1)
    elif r.get('title')=='retry motion' and float(marker.read_text())>time.time():data.update(status='retry',retryAfter=1)
    elif r.get('title')=='retry forever':
        marker.parent.mkdir(parents=True,exist_ok=True);counter=marker.parent/'attempt-count';counter.write_text(str(int(counter.read_text())+1 if counter.exists() else 1));data.update(status='retry',retryAfter=1)
    elif r.get('title')=='missing motion':data.update(status='unavailable')
    else:
        path=Path(r['artworkCache'])/'123.mp4';path.parent.mkdir(parents=True,exist_ok=True)
        source=os.environ.get('SUNG_MOTION_FIXTURE')
        if source:path.write_bytes(Path(source).read_bytes())
        else:path.write_bytes(b'fixture')
        data.update(status='ready',motionArt=path.as_uri())
elif op=='local-lyrics':data.update(lyrics='',lines=[])
elif op=='radio': data['items']=[song(i) for i in range(1,5)]
elif op in ('resolve','buffer'):
    import os, wave
    from pathlib import Path
    if os.environ.get('SUNG_BUFFER_FIXTURE') == '1':
        directory=Path(r['directory'])
        if r['id'].endswith('3'): time.sleep(.4)
        if r['id'].endswith('4'): data={'ok':False,'error':'Expected preparation failure'}
        else:
            path=directory/(r['id']+'.wav')
            with wave.open(str(path),'wb') as audio:
                audio.setnchannels(1);audio.setsampwidth(2);audio.setframerate(8000);audio.writeframes(b'\0'*2*8000*60)
            data.update(file=str(path),seconds=60)
    else: data={'ok':False,'error':'Fixture does not stream audio'}
else: data={'ok':False,'error':'Unknown fixture request'}
print(json.dumps(data))
