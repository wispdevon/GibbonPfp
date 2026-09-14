#!/usr/bin/env python3
"""Check a relocated distribution without Qt SDK environment fallbacks."""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile

if sys.platform == 'win32':
    import ctypes
    # Loader failures must return an error, rather than waiting on a modal dialog.
    ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)

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
    if sys.platform == 'win32' and shutil.which('dumpbin'):
        import re
        available = {p.name.lower() for p in exe.parent.glob('*.dll')}
        available.update(p.name.lower() for p in (pathlib.Path(os.environ['SystemRoot']) / 'System32').glob('*.dll'))
        missing = set()
        for binary in [exe, *exe.parent.glob('*.dll')]:
            imports = subprocess.check_output(['dumpbin', '/DEPENDENTS', str(binary)], text=True)
            for name in re.findall(r'^\s+([\w.-]+\.dll)\s*$', imports, re.MULTILINE | re.IGNORECASE):
                name = name.lower()
                if name not in available and not name.startswith(('api-ms-', 'ext-ms-')):
                    missing.add((binary.name, name))
        assert not missing, f'Missing Windows runtime dependencies: {sorted(missing)}'
    if sys.platform.startswith('linux'):
        from prune_linux_runtime import GLIBC
        assert not [p for p in target.rglob('*') if GLIBC.fullmatch(p.name)], 'Distribution must use host glibc'
        links = subprocess.check_output(['ldd', str(exe)], env=env, text=True)
        assert 'not found' not in links, links
        assert '/vcpkg_installed/' not in links and '/build-release/' not in links and '/Qt/' not in links, links
    subprocess.run([str(exe), '--version'], env=env, check=True, timeout=30)
    subprocess.run([str(exe), '--smoke-test'], env=env, check=True, timeout=60)
    subprocess.run([sys.executable, str(pathlib.Path(__file__).with_name('test_cli.py')), str(exe)], env=env, check=True, timeout=300)
print('Relocated distribution passed without Qt SDK search paths')
