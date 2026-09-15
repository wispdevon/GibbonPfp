#!/usr/bin/env python3
"""Verify pinned public portraits, optionally download, then run the shared engine."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', type=Path, default=ROOT / 'build-local/gibbon_benchmark')
    parser.add_argument('--photos', type=Path, default=Path.home() / '.cache/gibbon-benchmarks')
    parser.add_argument('--output', type=Path, default=Path.home() / '.cache/gibbon-benchmarks/reports/fast')
    parser.add_argument('--download', action='store_true', help='Download missing/corrupt public fixtures')
    parser.add_argument('--quality', action='store_true', help='Explicitly load the large High Quality model')
    parser.add_argument('--if-cached', action='store_true', help='Skip if public fixtures have not been downloaded')
    args = parser.parse_args()
    manifest = ROOT / 'benchmarks/portraits.json'
    portraits = json.loads(manifest.read_text())['portraits']
    if len(portraits) != 6:
        raise ValueError('Expected six public benchmark portraits')
    for p in portraits:
        if not p['license'].startswith(('CC0', 'CC BY ', 'CC BY-SA ')):
            raise ValueError('Unapproved fixture license')
        if urlparse(p['url']).hostname != 'upload.wikimedia.org' or not p['url'].startswith('https://'):
            raise ValueError('Expected a Wikimedia HTTPS download')
        if Path(p['file']).name != p['file']:
            raise ValueError('Invalid fixture filename')
        path = args.photos / p['file']
        valid = path.exists() and path.stat().st_size == p['bytes'] and hashlib.sha256(path.read_bytes()).hexdigest() == p['sha256']
        if not valid:
            if args.if_cached and not args.download:
                print('Public Fast benchmark skipped: run scripts/benchmark.py --download once.')
                return
            if not args.download:
                raise ValueError(f'{path} missing or checksum mismatch; use --download')
            args.photos.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(dir=args.photos, delete=False) as f:
                temporary = Path(f.name)
            try:
                subprocess.run(['curl', '--fail', '--location', '--silent', '--show-error',
                                '--retry', '2', '--max-time', '180', '--proto', '=https',
                                p['url'], '--output', str(temporary)], check=True)
                data = temporary.read_bytes()
                if len(data) != p['bytes'] or hashlib.sha256(data).hexdigest() != p['sha256']:
                    raise ValueError('Downloaded fixture checksum mismatch')
                temporary.replace(path)
            finally:
                temporary.unlink(missing_ok=True)
    args.output.mkdir(parents=True, exist_ok=True)
    command = [str(args.engine.resolve()), str(manifest), str(args.photos.resolve()), str(args.output.resolve())]
    if args.quality:
        command += ['--quality']
    subprocess.run(command, check=True, env={**os.environ, 'QT_QPA_PLATFORM': 'offscreen', 'QT_QPA_PLATFORMTHEME': 'generic'})
    report = json.loads((args.output / 'report.json').read_text())
    for photo in report['portraits']:
        cold, *warm = photo['runs']
        assert cold['cacheMisses'] > 0, photo['id']
        assert all(r['cacheHits'] > 0 and r['cacheMisses'] == 0 for r in warm), photo['id']
        assert all(not any(s['stage'] == 'Inference' for s in r['stages']) for r in warm)
    # Attribution travels with generated images, including source/license links and modification notice.
    text = '# Public portrait benchmark attributions\n\n'
    for p in portraits:
        text += f"- {p['attribution']}\n  Source: {p['source']}\n  License: {p['licenseUrl']}\n  {p['changes']}\n"
    (args.output / 'ATTRIBUTIONS.md').write_text(text)
    print(f"Verified cold/warm cache behavior. Visually inspect {args.output / 'contact-sheet.png'}")

if __name__ == '__main__':
    main()
