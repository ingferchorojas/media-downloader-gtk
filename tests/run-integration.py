#!/usr/bin/env python3
"""Serve only locally generated media; exercise real yt-dlp and FFmpeg."""
import functools
import http.server
import pathlib
import subprocess
import sys
import os
import tempfile
import threading

with tempfile.TemporaryDirectory(prefix='md-integration-') as tmp:
    root = pathlib.Path(tmp)
    media = root / 'own test.mp4'
    subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'testsrc=size=160x90:rate=10',
                    '-f', 'lavfi', '-i', 'sine=frequency=440', '-t', '3', '-c:v', 'libx264',
                    '-pix_fmt', 'yuv420p', '-c:a', 'aac', str(media)], check=True)
    subprocess.run(['ffmpeg', '-v', 'error', '-i', str(media), '-frames:v', '1', str(root / 'cover.jpg')], check=True)
    # MPEG-DASH exposes actual separate codec metadata for merge/remux checks.
    dash = root / 'dash'
    dash.mkdir()
    subprocess.run(['ffmpeg', '-v', 'error', '-i', str(media), '-map', '0:v', '-map', '0:a',
                    '-c', 'copy', '-f', 'dash', str(dash / 'index.mpd')], check=True)
    webm = root / 'webm-dash'
    webm.mkdir()
    subprocess.run(['ffmpeg', '-v', 'error', '-i', str(media), '-map', '0:v', '-map', '0:a',
                    '-c:v', 'libvpx-vp9', '-c:a', 'libopus', '-f', 'dash',
                    str(webm / 'index.mpd')], check=True)
    handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=tmp)
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = f'http://127.0.0.1:{server.server_port}/own%20test.mp4'
    (root / 'own-test.html').write_text(
        '<html><head><title>own-test</title><meta property="og:image" '
        f'content="http://127.0.0.1:{server.server_port}/cover.jpg"></head>'
        '<body><video controls src="own%20test.mp4" poster="cover.jpg"></video></body></html>')
    page_url = f'http://127.0.0.1:{server.server_port}/own-test.html'
    binary = str(pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'build/test-integration').resolve())
    try:
        for ext in ([] if (os.environ.get('MD_ONLY_UI') or os.environ.get('MD_FORMAT_REGRESSIONS_ONLY')) else ['mp4', 'mp3', 'm4a', 'opus', 'flac', 'wav']):
            # Direct generic media lacks codec metadata: only its native video
            # container is selected; cross-container cases use detailed fixtures.
            if ext in ['webm', 'mkv']:
                continue
            dest = root / ext
            dest.mkdir()
            subprocess.run([binary, url, str(dest), ext], check=True, timeout=50)
            outputs = list(dest.glob(f'*.{ext}'))
            assert outputs, f'Missing {ext} output'
            assert 'own test' in outputs[0].name, outputs[0].name
            subprocess.run(['ffprobe', '-v', 'error', str(outputs[0])], check=True)
        for ext, manifest in ([] if (os.environ.get('MD_ONLY_UI') or os.environ.get('MD_FORMAT_REGRESSIONS_ONLY')) else [('mp4', 'dash'), ('mkv', 'dash'), ('webm', 'webm-dash')]):
            dest = root / f'merged-{ext}'
            dest.mkdir()
            dash_url = f'http://127.0.0.1:{server.server_port}/{manifest}/index.mpd'
            subprocess.run([binary, dash_url, str(dest), ext], check=True, timeout=60)
            output = next(dest.glob(f'*.{ext}'))
            probe = subprocess.check_output(['ffprobe', '-v', 'error', '-show_entries',
                                             'stream=codec_type', '-of', 'csv=p=0', str(output)], text=True)
            assert 'video' in probe and 'audio' in probe
        if not os.environ.get('MD_ONLY_UI') and not os.environ.get('MD_FORMAT_REGRESSIONS_ONLY'):
            dest = root / 'numbered'
            dest.mkdir()
            subprocess.run([binary, url, str(dest), 'mp4', 'numbered'], check=True, timeout=50)
            output = next(dest.glob('007 - *.mp4'))
            subprocess.run(['ffprobe', '-v', 'error', str(output)], check=True)
        for mode in ([] if (os.environ.get('MD_ONLY_UI') or os.environ.get('MD_FORMAT_REGRESSIONS_ONLY')) else ['queue', 'cancel', 'error']):
            dest = root / mode
            dest.mkdir()
            test_url = url if mode != 'error' else url + '/missing'
            subprocess.run([binary, test_url, str(dest), 'mp4', mode], check=True, timeout=65)
        if not os.environ.get('MD_ONLY_UI'):
            dest = root / 'playlist-mp3'
            dest.mkdir()
            manifest = f'http://127.0.0.1:{server.server_port}/dash/index.mpd'
            subprocess.run([binary, url, str(dest), 'mp3', 'playlist-mp3', manifest],
                           check=True, timeout=65)
            outputs = sorted(dest.glob('*.mp3'))
            assert len(outputs) == 4
            assert [p.name[:3] for p in outputs] == ['001', '002', '003', '004']
            assert not list(dest.glob('*.mp4')) and not list(dest.glob('*.webm'))
            for output in outputs:
                import json
                probe = json.loads(subprocess.check_output([
                    'ffprobe', '-v', 'error', '-show_entries',
                    'format=format_name:stream=codec_type,codec_name', '-of', 'json', str(output)], text=True))
                assert probe['format']['format_name'] == 'mp3'
                assert all(s['codec_type'] == 'audio' and s['codec_name'] == 'mp3' for s in probe['streams'])
            # Preserve a valid existing MP3, and reject a video disguised as an MP3.
            existing_dir = root / 'existing-mp3'
            existing_dir.mkdir()
            subprocess.run([binary, url, str(existing_dir), 'mp3'], check=True, timeout=50)
            existing = next(existing_dir.glob('*.mp3'))
            original = existing.read_bytes()
            subprocess.run([binary, url, str(existing_dir), 'mp3'], check=True, timeout=50)
            assert existing.read_bytes() == original
            conflicting_dir = root / 'conflicting-mp3'
            conflicting_dir.mkdir()
            conflicting = conflicting_dir / existing.name
            conflicting.write_bytes(media.read_bytes())
            subprocess.run([binary, url, str(conflicting_dir), 'mp3', 'invalid-output'],
                           check=True, timeout=50)
            assert conflicting.read_bytes() == media.read_bytes()
            # An engine that exits successfully but leaves a video must fail verification.
            fake_data = root / 'fake-data'
            engine = fake_data / 'media-downloader-gtk/bin/yt-dlp'
            engine.parent.mkdir(parents=True)
            engine.write_text(f'#!{sys.executable}\n' +
                "import sys,json,pathlib,shutil,os\n"
                "args=sys.argv[1:]\n"
                "if '--version' in args: print('2025.09.05')\n"
                "elif '--help' in args: pass\n"
                "elif '--dump-single-json' in args: print(json.dumps({'title':'fake','duration':3,'formats':[{'format_id':'http','ext':'mp4','vcodec':'h264','acodec':'aac','height':90}]}))\n"
                "else:\n"
                " dest=pathlib.Path(args[args.index('--paths')+1][5:])/'fake.mp4'\n"
                " shutil.copyfile(os.environ['MD_FAKE_SOURCE'],dest)\n"
                " print('MD_OUTPUT:'+json.dumps(str(dest)))\n")
            engine.chmod(0o700)
            dest = root / 'invalid-output'
            dest.mkdir()
            env = os.environ.copy()
            env.update(XDG_DATA_HOME=str(fake_data), MD_FAKE_SOURCE=str(media))
            subprocess.run([binary, url, str(dest), 'mp3', 'invalid-output'],
                           env=env, check=True, timeout=30)
            assert not list(dest.glob('*.mp4')) and not list(dest.glob('*.mp3'))
        if os.environ.get('MD_TEST_UI'):
            dest = root / 'ui'
            dest.mkdir()
            env = os.environ.copy()
            env['XDG_CONFIG_HOME'] = str(root / 'config')
            subprocess.run([str(pathlib.Path(binary).with_name('test-ui')), page_url, str(dest),
                            str(pathlib.Path('build/screenshot.png').resolve())],
                           env=env, check=True, timeout=100)
            assert list(dest.glob('*.mp4'))
        # A second run must preserve existing files.
        if os.environ.get('MD_ONLY_UI') or os.environ.get('MD_FORMAT_REGRESSIONS_ONLY'):
            sys.exit(0)
        native = next((root / 'mp4').glob('*.mp4'))
        original = native.read_bytes()
        subprocess.run([binary, url, str(root / 'mp4'), 'mp4'], check=True, timeout=50)
        assert native.read_bytes() == original
    finally:
        server.shutdown()
