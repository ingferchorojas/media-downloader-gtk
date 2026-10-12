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
    media = root / 'own-test.mp4'
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
    url = f'http://127.0.0.1:{server.server_port}/own-test.mp4'
    (root / 'own-test.html').write_text(
        '<html><head><title>own-test</title><meta property="og:image" '
        f'content="http://127.0.0.1:{server.server_port}/cover.jpg"></head>'
        '<body><video controls src="own-test.mp4" poster="cover.jpg"></video></body></html>')
    page_url = f'http://127.0.0.1:{server.server_port}/own-test.html'
    binary = str(pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'build/test-integration').resolve())
    try:
        for ext in ([] if os.environ.get('MD_ONLY_UI') else ['mp4', 'mp3', 'm4a', 'opus', 'flac', 'wav']):
            # Direct generic media lacks codec metadata: only its native video
            # container is selected; cross-container cases use detailed fixtures.
            if ext in ['webm', 'mkv']:
                continue
            dest = root / ext
            dest.mkdir()
            subprocess.run([binary, url, str(dest), ext], check=True, timeout=50)
            outputs = list(dest.glob(f'*.{ext}'))
            assert outputs, f'Missing {ext} output'
            subprocess.run(['ffprobe', '-v', 'error', str(outputs[0])], check=True)
        for ext, manifest in ([] if os.environ.get('MD_ONLY_UI') else [('mp4', 'dash'), ('mkv', 'dash'), ('webm', 'webm-dash')]):
            dest = root / f'merged-{ext}'
            dest.mkdir()
            dash_url = f'http://127.0.0.1:{server.server_port}/{manifest}/index.mpd'
            subprocess.run([binary, dash_url, str(dest), ext], check=True, timeout=60)
            output = next(dest.glob(f'*.{ext}'))
            probe = subprocess.check_output(['ffprobe', '-v', 'error', '-show_entries',
                                             'stream=codec_type', '-of', 'csv=p=0', str(output)], text=True)
            assert 'video' in probe and 'audio' in probe
        for mode in ([] if os.environ.get('MD_ONLY_UI') else ['queue', 'cancel', 'error']):
            dest = root / mode
            dest.mkdir()
            test_url = url if mode != 'error' else url + '/missing'
            subprocess.run([binary, test_url, str(dest), 'mp4', mode], check=True, timeout=65)
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
        if os.environ.get('MD_ONLY_UI'):
            sys.exit(0)
        native = next((root / 'mp4').glob('*.mp4'))
        original = native.read_bytes()
        subprocess.run([binary, url, str(root / 'mp4'), 'mp4'], check=True, timeout=50)
        assert native.read_bytes() == original
    finally:
        server.shutdown()
