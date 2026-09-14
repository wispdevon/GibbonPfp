import pathlib
import shutil
import sys

source, target = map(pathlib.Path, sys.argv[1:])
for notice in source.glob('*/copyright'):
    output = target / notice.parent.name
    output.mkdir(parents=True, exist_ok=True)
    shutil.copy2(notice, output / 'LICENSE')
