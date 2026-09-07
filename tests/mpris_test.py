"""Isolated D-Bus integration test; never controls the user's running player."""
import os, pathlib, subprocess, sys, time
binary=sys.argv[1]
service='org.mpris.MediaPlayer2.sung';path='/org/mpris/MediaPlayer2'
proc=subprocess.Popen([binary,'--isolated','--mpris-test','--offline'])
def call(method,*args):
    return subprocess.check_output(['qdbus6',service,path,method,*args],text=True,stderr=subprocess.DEVNULL).strip()
def prop(name): return call('org.freedesktop.DBus.Properties.Get','org.mpris.MediaPlayer2.Player',name)
def setprop(name,value): return call('org.freedesktop.DBus.Properties.Set','org.mpris.MediaPlayer2.Player',name,value)
try:
    for _ in range(100):
        try:
            identity=call('org.mpris.MediaPlayer2.Identity');break
        except subprocess.CalledProcessError:time.sleep(.1)
    else:raise AssertionError('MPRIS registration timed out')
    assert identity=='Sung';print('PASS MPRIS identity')
    assert prop('PlaybackStatus')=='Stopped';print('PASS stopped state')
    assert float(prop('MinimumRate'))==0.5 and float(prop('MaximumRate'))==2;print('PASS rate limits')
    setprop('Rate','1.25');assert float(prop('Rate'))==1.25;print('PASS playback rate round trip')
    setprop('Rate','-1');assert float(prop('Rate'))==1.25;print('PASS invalid playback rate ignored')
    setprop('Rate','0');assert prop('PlaybackStatus')=='Stopped';setprop('Rate','1')
    setprop('Volume','0.23');assert abs(float(prop('Volume'))-.23)<.01;print('PASS volume round trip')
    setprop('Shuffle','true');assert prop('Shuffle')=='true';print('PASS shuffle round trip')
    setprop('LoopStatus','Track');assert prop('LoopStatus')=='Track';print('PASS repeat round trip')
    for action in ['Pause','Stop','Play','Next','Previous']:
        call('org.mpris.MediaPlayer2.Player.'+action)
    assert prop('PlaybackStatus')=='Stopped';print('PASS empty queue transport safety')
    call('org.mpris.MediaPlayer2.Raise');print('PASS raise')
    call('org.mpris.MediaPlayer2.Quit');proc.wait(timeout=5);assert proc.returncode==0;print('PASS quit')
finally:
    if proc.poll() is None:proc.terminate();proc.wait(timeout=5)
