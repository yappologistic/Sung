#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
prefix="${1:-$HOME/.local}"
"$root/scripts/build.sh"
cmake --install "$root/build" --prefix "$prefix"
python3 -m venv "$prefix/lib/sung/runtime"
"$prefix/lib/sung/runtime/bin/python" -m pip install --disable-pip-version-check -r "$prefix/lib/sung/requirements.txt"
if command -v update-desktop-database >/dev/null; then update-desktop-database "$prefix/share/applications"; fi
printf 'Installed Sung to %s/bin/sung\n' "$prefix"
