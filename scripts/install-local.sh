#!/usr/bin/env bash
# Build, verify, and replace the per-user test installation (no sudo).
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
install_prefix="${HEADSHOTFLOW_INSTALL_PREFIX:-$HOME/.local}"
build_dir="$project_dir/build-local"
build_version="$(git -C "$project_dir" rev-parse --short HEAD).local.$(date -u +%Y%m%d%H%M%S)"
python3 "$project_dir/scripts/fetch_assets.py"
cmake -S "$project_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$install_prefix" \
    -DHEADSHOTFLOW_SYSTEM_INSTALL=ON -DHEADSHOTFLOW_BUNDLED_ORT=OFF -DBUILD_TESTING=ON \
    -DHEADSHOTFLOW_BUILD_VERSION="$build_version"
cmake --build "$build_dir" --parallel "${HEADSHOTFLOW_BUILD_JOBS:-3}"
ctest --test-dir "$build_dir" --output-on-failure
python3 "$project_dir/scripts/test_cli.py" "$build_dir/headshotflow"
python3 "$project_dir/scripts/benchmark.py" --engine "$build_dir/headshotflow_benchmark" --if-cached
cmake --install "$build_dir"
# Desktop launchers do not always inherit ~/.local/bin in PATH.
python3 - "$install_prefix" <<'PY'
from pathlib import Path
import sys
prefix=Path(sys.argv[1])
path=prefix/'share/applications/headshotflow.desktop'
exe=str(prefix/'bin/headshotflow')
exe=exe.replace('\\','\\\\').replace('"','\\"').replace('`','\\`').replace('$','\\$').replace('%','%%')
path.write_text(path.read_text().replace('Exec=headshotflow %F', f'Exec="{exe}" %F'))
previous = path.with_name("gibbonpfp.desktop")
if previous.is_file() and not previous.is_symlink():
    fields = previous.read_text().splitlines()
    if "Name=HeadshotFlow" in fields and "Icon=gibbonpfp" in fields:
        previous.unlink()
PY
if command -v update-desktop-database >/dev/null; then
    update-desktop-database "$install_prefix/share/applications"
fi
HEADSHOTFLOW_DISABLE_SOURCE_MODELS=1 QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
    "$install_prefix/bin/headshotflow" --smoke-test
# Display-backed Qt platform themes initialize differently from the offscreen plugin.
if command -v xvfb-run >/dev/null; then
    xvfb-run -a env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb HEADSHOTFLOW_DISABLE_SOURCE_MODELS=1 "$install_prefix/bin/headshotflow" --smoke-test
else
    echo "Skipping display-backed smoke test: xvfb-run unavailable" >&2
fi
HEADSHOTFLOW_DISABLE_SOURCE_MODELS=1 python3 "$project_dir/scripts/test_cli.py" "$install_prefix/bin/headshotflow"
printf 'Installed current workspace: %s/bin/headshotflow\n' "$install_prefix"
