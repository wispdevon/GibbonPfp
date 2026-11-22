#!/usr/bin/env bash
set -euo pipefail
build_dir="${1:-build-release}"
version=$(sed -n 's/^CMAKE_PROJECT_VERSION:STATIC=//p' "$build_dir/CMakeCache.txt")
: "${version:?Missing project version in CMake cache}"
mkdir -p dist/tools
stage_dir=$(mktemp -d "$PWD/dist/AppDir.XXXXXX")
trap 'rm -rf -- "$stage_dir"' EXIT
cmake --install "$build_dir" --prefix "$stage_dir/usr"
python3 scripts/prune_linux_runtime.py "$stage_dir/usr"
cp assets/gibbonpfp.desktop "$stage_dir/"
cp assets/gibbonpfp.svg "$stage_dir/"
curl -fL https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage -o dist/tools/linuxdeploy.AppImage
echo 'c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d  dist/tools/linuxdeploy.AppImage' | sha256sum -c -
chmod +x dist/tools/linuxdeploy.AppImage
export APPIMAGE_EXTRACT_AND_RUN=1
export VERSION="$version"
export OUTPUT="$PWD/dist/HeadshotFlow-$version-x86_64.AppImage"
# linuxdeploy's bundled strip cannot read newer toolchains' RELR sections.
NO_STRIP=1 dist/tools/linuxdeploy.AppImage --appdir "$stage_dir" --executable "$stage_dir/usr/bin/gibbonpfp" --custom-apprun scripts/apprun.sh --desktop-file assets/gibbonpfp.desktop --icon-file assets/gibbonpfp.svg --output appimage
"$OUTPUT" --version
GIBBON_DISABLE_SOURCE_MODELS=1 "$OUTPUT" models list >/dev/null
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software "$OUTPUT" --smoke-test
