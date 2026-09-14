#!/usr/bin/env python3
"""Keep the host's glibc and loader together in generated Linux distributions."""
import pathlib
import re
import sys

GLIBC = re.compile(r'^(?:lib(?:c|m|pthread|dl|rt|util|resolv|nss_[^.]+)\.so(?:\..*)?|ld-linux.*|ld-[0-9].*\.so)$')


def prune(root):
    for path in pathlib.Path(root).rglob('*'):
        if GLIBC.fullmatch(path.name) and (path.is_file() or path.is_symlink()):
            print(f'Use host glibc: {path}')
            path.unlink()


if __name__ == '__main__':
    prune(sys.argv[1])
