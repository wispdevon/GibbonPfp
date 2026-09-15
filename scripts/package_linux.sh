#!/usr/bin/env bash
set -euo pipefail
build_dir="${1:-build-release}"
stage_dir="$PWD/dist/AppDir"
cmake --install "$build_dir" --prefix "$stage_dir/usr"
python3 scripts/prune_linux_runtime.py "$stage_dir/usr"
cp assets/gibbonpfp.desktop "$stage_dir/"
cp assets/gibbonpfp.svg "$stage_dir/"
mkdir -p dist/tools
curl -fL https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage -o dist/tools/linuxdeploy.AppImage
echo '36a2d7e274d12e1050d0e9ecfe11d339ed54720b2bec464c286d53f8b07f5c62  dist/tools/linuxdeploy.AppImage' | sha256sum -c -
chmod +x dist/tools/linuxdeploy.AppImage
export APPIMAGE_EXTRACT_AND_RUN=1
export VERSION=1.0.1
export OUTPUT="$PWD/dist/HeadshotFlow-1.0.1-x86_64.AppImage"
dist/tools/linuxdeploy.AppImage --appdir "$stage_dir" --executable "$stage_dir/usr/bin/gibbonpfp" --desktop-file assets/gibbonpfp.desktop --icon-file assets/gibbonpfp.svg --output appimage
"$OUTPUT" --version
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software "$OUTPUT" --smoke-test
