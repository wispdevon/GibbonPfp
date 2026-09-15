#!/usr/bin/env bash
# Package the current workspace, including uncommitted source edits.
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
package_dir="$project_dir/build-arch-local"
mkdir -p "$package_dir"
python3 "$project_dir/scripts/fetch_assets.py"
python3 - "$project_dir" "$package_dir" <<'PY'
from pathlib import Path
import datetime, hashlib, re, shutil, subprocess, sys, tarfile
root, dest=map(Path,sys.argv[1:])
def git(*args):
    return subprocess.check_output(['git','-C',str(root),*args]).decode().strip()
archive=dest/'GibbonPfp.tar.gz'
with tarfile.open(archive,'w:gz') as tar:
    for name in sorted(set(git('ls-files','--cached','--others','--exclude-standard','-z').split('\0'))):
        if name and (root/name).is_file():
            tar.add(root/name,arcname='GibbonPfp/'+name,recursive=False)
recipe=(root/'packaging/aur/PKGBUILD').read_text()
version=re.search(r'project\(GibbonPfp VERSION (\S+)',(root/'CMakeLists.txt').read_text())[1]
version+=f'.r{git("rev-list","--count","HEAD")}.g{git("rev-parse","--short","HEAD")}.local'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d%H%M%S')
recipe=re.sub(r'^pkgver=.*$',f'pkgver={version}',recipe,flags=re.M)
recipe=recipe[:recipe.index('pkgver() {')]+recipe[recipe.index('prepare() {'):]
recipe=recipe.replace('GibbonPfp::git+https://github.com/wispdevon/GibbonPfp.git',archive.name)
recipe=recipe.replace("sha256sums=('SKIP'",f"sha256sums=('{hashlib.sha256(archive.read_bytes()).hexdigest()}'")
(dest/'PKGBUILD').write_text(recipe)
for name in ('face_detection_yunet_2023mar.onnx','human_segmentation_pphumanseg_2023mar.onnx'):
    shutil.copy2(root/'assets/models'/name,dest/name)
PY
cd "$package_dir"
makepkg --cleanbuild --force
printf 'Arch package ready in %s (install with sudo pacman -U <package>).\n' "$package_dir"
