#!/usr/bin/env python3
"""Check a relocated distribution without Qt SDK environment fallbacks."""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

root = pathlib.Path(sys.argv[1]).resolve()
relative = pathlib.Path(sys.argv[2])
env = dict(os.environ)
for key in ('QT_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'QML_IMPORT_PATH', 'LD_LIBRARY_PATH',
            'DYLD_LIBRARY_PATH', 'DYLD_FRAMEWORK_PATH', 'QT_ROOT_DIR', 'GIBBON_MODEL_DIR'):
    env.pop(key, None)
env.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
           QT_QPA_PLATFORMTHEME='', GIBBON_DISABLE_SOURCE_MODELS='1')
env['PATH'] = os.pathsep.join(p for p in env.get('PATH', '').split(os.pathsep)
                           if not any(s in p.replace('\\', '/').lower()
                                      for s in ('/qt/', '/vcpkg_installed/', '/build-release')))
with tempfile.TemporaryDirectory(prefix='gibbon-relocated-') as temp:
    target = pathlib.Path(temp) / 'distribution'
    shutil.copytree(root, target, symlinks=True)
    exe = target / relative
    if sys.platform.startswith('linux'):
        links = subprocess.check_output(['ldd', str(exe)], env=env, text=True)
        assert 'not found' not in links, links
        assert '/vcpkg_installed/' not in links and '/build-release/' not in links and '/Qt/' not in links, links
    subprocess.run([str(exe), '--version'], env=env, check=True, timeout=30)
    subprocess.run([str(exe), '--smoke-test'], env=env, check=True, timeout=60)
    subprocess.run([sys.executable, str(pathlib.Path(__file__).with_name('test_cli.py')), str(exe)], env=env, check=True, timeout=300)
print('Relocated distribution passed without Qt SDK search paths')
