#!/usr/bin/env bash
# Build, verify, and replace the per-user test installation (no sudo).
set -euo pipefail
project_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
install_prefix="${GIBBON_INSTALL_PREFIX:-$HOME/.local}"
build_dir="$project_dir/build-local"
build_version="$(git -C "$project_dir" rev-parse --short HEAD).local.$(date -u +%Y%m%d%H%M%S)"
python3 "$project_dir/scripts/fetch_assets.py"
cmake -S "$project_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$install_prefix" \
    -DGIBBON_SYSTEM_INSTALL=ON -DGIBBON_BUNDLED_ORT=OFF -DBUILD_TESTING=ON \
    -DGIBBON_BUILD_VERSION="$build_version"
cmake --build "$build_dir" --parallel "${GIBBON_BUILD_JOBS:-3}"
ctest --test-dir "$build_dir" --output-on-failure
python3 "$project_dir/scripts/test_cli.py" "$build_dir/gibbonpfp"
cmake --install "$build_dir"
# Desktop launchers do not always inherit ~/.local/bin in PATH.
python3 - "$install_prefix" <<'PY'
from pathlib import Path
import sys
prefix=Path(sys.argv[1])
path=prefix/'share/applications/gibbonpfp.desktop'
exe=str(prefix/'bin/gibbonpfp')
exe=exe.replace('\\','\\\\').replace('"','\\"').replace('`','\\`').replace('$','\\$').replace('%','%%')
path.write_text(path.read_text().replace('Exec=gibbonpfp %F', f'Exec="{exe}" %F'))
PY
if command -v update-desktop-database >/dev/null; then
    update-desktop-database "$install_prefix/share/applications"
fi
GIBBON_DISABLE_SOURCE_MODELS=1 QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
    "$install_prefix/bin/gibbonpfp" --smoke-test
GIBBON_DISABLE_SOURCE_MODELS=1 python3 "$project_dir/scripts/test_cli.py" "$install_prefix/bin/gibbonpfp"
printf 'Installed current workspace: %s/bin/gibbonpfp\n' "$install_prefix"
