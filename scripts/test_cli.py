#!/usr/bin/env python3
"""Exercise the installed CLI without Python imaging dependencies."""
import json
import pathlib
import struct
import subprocess
import sys
import tempfile
import zlib

def png(path, width, height):
    def chunk(tag, payload):
        return struct.pack('!I', len(payload)) + tag + payload + struct.pack('!I', zlib.crc32(tag + payload))
    row = b'\x00' + bytes((160, 128, 100)) * width
    compressor = zlib.compressobj()
    compressed = bytearray()
    for _ in range(height):
        compressed += compressor.compress(row)
    compressed += compressor.flush()
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!2I5B', width, height, 8, 2, 0, 0, 0)) + chunk(b'IDAT', compressed) + chunk(b'IEND', b''))

def main():
    exe = str(pathlib.Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='gibbon-cli-') as temp:
        root = pathlib.Path(temp)
        photo = root / 'portrait ü.png'
        png(photo, 600, 800)
        def run(*args, code=0):
            result = subprocess.run([exe, *map(str, args)], text=True, capture_output=True, timeout=180)
            assert result.returncode == code, (args, result.returncode, result.stdout, result.stderr)
            return result.stdout
        diagnostic = json.loads(run('process', photo, '--output', root / 'diagnostic', '--no-auto-crop'))['diagnostics']
        assert diagnostic['totalMs'] > 0
        assert diagnostic['stages'][0]['stage'] == 'Decoding'
        assert diagnostic['stages'][-1]['stage'] == 'Encoding'
        assert diagnostic['cacheHits'] == 0
        assert 'device' in diagnostic
        run('--version')
        run('-v')
        run('-h')
        for level in ('off', 'low', 'standard', 'high'):
            run('process', photo, '--output', root / ('sharpen-' + level), '--no-auto-crop', '--screen-sharpening', level)
        run('process', photo, '--output', root / 'invalid-sharpen', '--screen-sharpening', 'invalid', code=1)
        raw = pathlib.Path(__file__).resolve().parents[1] / 'tests/fixtures/synthetic.dng'
        run('process', raw, '--output', root / 'raw', '--no-auto-crop', '--white-balance', 'custom', '--temperature', '5500', '--tint', '1.05', '--highlight', '3')
        heic = raw.with_name('synthetic.heic')
        run('process', heic, '--output', root / 'heic', '--no-auto-crop', '--format', 'png')
        assert struct.unpack('!II', next((root / 'heic').glob('*.png')).read_bytes()[16:24]) == (120, 160)
        models = json.loads(run('models', 'list'))
        assert {m['id'] for m in models} == {'face', 'fast', 'quality'}
        report = root / 'report.json'
        run('process', photo, '--output', root / 'out', '--report', report, code=2)
        assert json.loads(report.read_text())[0]['status'] == 'review'
        assert not list((root / 'out').glob('*'))
        run('process', photo, '--output', root / 'out', '--fully-automatic', '--format', 'png')
        first = next((root / 'out').glob('*.png'))
        assert struct.unpack('!II', first.read_bytes()[16:24]) == (360, 480)
        run('process', photo, '--output', root / 'out', '--fully-automatic', '--format', 'png')
        assert len(list((root / 'out').glob('*.png'))) == 2
        run('process', photo, '--output', root / 'source', '--no-auto-crop', '--uncapped', '--format', 'png')
        assert struct.unpack('!II', next((root / 'source').glob('*.png')).read_bytes()[16:24]) == (600, 800)
        run('process', photo, '--output', root / 'bad', '--size', '480x360', code=1)
        broken = root / 'broken.jpg'
        broken.write_text('invalid')
        run('process', photo, broken, '--output', root / 'partial', '--no-auto-crop', '--report', root / 'partial.csv', code=2)
        assert 'failed' in (root / 'partial.csv').read_text()
        assert photo.exists()
        for name, w, h in [('16mp', 4896, 3264), ('24mp', 6000, 4000)]:
            big = root / (name + '.png')
            png(big, w, h)
            run('process', big, '--output', root / 'large', '--no-auto-crop')
    print('CLI integration: RAW/HEIC, review gates, Unicode paths, sizing, collisions, failures, 16MP/24MP passed')

if __name__ == '__main__':
    main()
