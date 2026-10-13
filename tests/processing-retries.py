#!/usr/bin/env python3
"""Exercise processing retries with a downloaded fixture and real FFmpeg conversion."""
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

binary = str(pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'build-release/test-integration').resolve())
with tempfile.TemporaryDirectory(prefix='md-processing-retries-') as tmp:
    root = pathlib.Path(tmp)
    source = root / 'source.mp4'
    subprocess.run(['ffmpeg', '-nostdin', '-v', 'error', '-f', 'lavfi', '-i',
                    'sine=frequency=440', '-t', '0.2', '-c:a', 'aac', str(source)], check=True)
    data = root / 'data'
    engine = data / 'media-downloader-gtk/bin/yt-dlp'
    engine.parent.mkdir(parents=True)
    engine.write_text(f'#!{sys.executable}\n' + '''import json,os,pathlib,shutil,subprocess,sys
args=sys.argv[1:]
if '--version' in args:
 print('2025.09.05')
elif '--help' in args:
 pass
elif '--dump-single-json' in args:
 if os.environ.get('MD_ANALYSIS_COUNTER'):
  counter=pathlib.Path(os.environ['MD_ANALYSIS_COUNTER'])
  count=int(counter.read_text())+1 if counter.exists() else 1
  counter.write_text(str(count))
  if count<=int(os.environ.get('MD_ANALYSIS_FAIL_COUNT','0')):
   print('ERROR: Unable to download webpage: HTTP Error '+os.environ.get('MD_ANALYSIS_ERROR','503'),file=sys.stderr)
   sys.exit(1)
 print(json.dumps({'title':'fixture','duration':0.2,'formats':[{'format_id':'http','ext':'mp4','vcodec':'none','acodec':'aac'}]}))
else:
 counter=pathlib.Path(os.environ['MD_COUNTER'])
 state=json.loads(counter.read_text()) if counter.exists() else {'attempts':0,'downloads':0}
 state['attempts']+=1
 work=pathlib.Path(args[args.index('--paths')+1][5:])
 source=work/'fixture.mp4'
 if not source.exists():
  shutil.copyfile(os.environ['MD_SOURCE'],source)
  state['downloads']+=1
 counter.write_text(json.dumps(state))
 print('MD_PROGRESS:'+json.dumps({'postprocessor':'ExtractAudio','status':'started'}),flush=True)
 if state['attempts']<=int(os.environ['MD_FAIL_COUNT']):
  print('ERROR: FFmpeg simulated conversion failure',flush=True)
  sys.exit(1)
 target=work/'fixture.mp3'
 subprocess.run([os.environ['MD_REAL_FFMPEG'],'-nostdin','-v','error','-y','-i',str(source),'-vn','-c:a','libmp3lame',str(target)],check=True)
 print('MD_OUTPUT:'+json.dumps(str(target)),flush=True)
''')
    engine.chmod(0o700)
    for retries in (1, 2, 3):
        for exhausted in (False, True):
            name = f'{retries}-' + ('exhausted' if exhausted else 'recovered')
            destination = root / name
            destination.mkdir()
            counter = root / (name + '.json')
            env = os.environ.copy()
            env.update(XDG_DATA_HOME=str(data), MD_SOURCE=str(source), MD_COUNTER=str(counter),
                       MD_FAIL_COUNT=str(99 if exhausted else 1),
                       MD_REAL_FFMPEG=shutil.which('ffmpeg'), MD_TEST_PROCESSING_RETRIES=str(retries))
            mode = 'processing-exhausted' if exhausted else 'download'
            subprocess.run([binary, 'https://example.com/fixture', str(destination), 'mp3', mode],
                           env=env, check=True, timeout=30)
            state = json.loads(counter.read_text())
            assert state['downloads'] == 1, state
            assert state['attempts'] == (retries + 1 if exhausted else 2), state
            assert bool(list(destination.glob('*.mp3'))) == (not exhausted)
            assert not list(destination.glob('*.mp4'))
    destination = root / 'cancel'
    destination.mkdir()
    counter = root / 'cancel.json'
    env.update(MD_COUNTER=str(counter), MD_FAIL_COUNT='99', MD_TEST_PROCESSING_RETRIES='3')
    subprocess.run([binary, 'https://example.com/fixture', str(destination), 'mp3', 'processing-cancel'],
                   env=env, check=True, timeout=30)
    assert json.loads(counter.read_text()) == {'attempts': 1, 'downloads': 1}
    assert not list(destination.glob('*.mp3'))
    for label, retries, failures, error, mode, analyses in [
        ('analysis-recovered', 3, 2, '503', 'download', 3),
        ('analysis-exhausted', 1, 99, '503', 'analysis-exhausted', 2),
        ('analysis-permanent', 10, 99, '404', 'analysis-exhausted', 1),
        ('analysis-cancel', 3, 99, '503', 'analysis-cancel', 1),
    ]:
        destination = root / label
        destination.mkdir()
        counter = root / (label + '.json')
        analysis_counter = root / (label + '-count.txt')
        env.update(MD_COUNTER=str(counter), MD_FAIL_COUNT='0', MD_TEST_PROCESSING_RETRIES=str(retries),
                   MD_ANALYSIS_COUNTER=str(analysis_counter), MD_ANALYSIS_FAIL_COUNT=str(failures),
                   MD_ANALYSIS_ERROR=error)
        subprocess.run([binary, 'https://example.com/fixture', str(destination), 'mp3', mode],
                       env=env, check=True, timeout=30)
        assert int(analysis_counter.read_text()) == analyses
        assert bool(list(destination.glob('*.mp3'))) == (mode == 'download')
print('Processing retry counts, cached download reuse, exhaustion, cancellation and transient analysis failures verified.')
