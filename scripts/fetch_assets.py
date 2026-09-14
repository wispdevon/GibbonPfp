#!/usr/bin/env python3
"""Fetch checksum-pinned model assets. Quality is optional and never bundled."""
import argparse
import hashlib
import json
import pathlib
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]

def fetch(entry, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists() and hashlib.sha256(destination.read_bytes()).hexdigest() == entry['sha256']:
        return
    temp = destination.with_suffix('.download')
    try:
        h = hashlib.sha256()
        with urllib.request.urlopen(entry['url'], timeout=120) as response, temp.open('wb') as output:
            while chunk := response.read(1024 * 1024):
                h.update(chunk)
                output.write(chunk)
        if h.hexdigest() != entry['sha256']:
            raise RuntimeError(f"Checksum mismatch: {destination.name}")
        temp.replace(destination)
    finally:
        temp.unlink(missing_ok=True)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--quality', action='store_true')
    args = parser.parse_args()
    for model in json.loads((ROOT / 'assets/models/manifest.json').read_text())['models']:
        if model['id'] == 'quality' and not args.quality:
            continue
        print('Fetching', model['id'], flush=True)
        fetch(model, ROOT / 'assets/models' / model['file'])
