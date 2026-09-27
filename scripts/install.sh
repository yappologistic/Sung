#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
prefix="${1:-$HOME/.local}"
"$root/scripts/build.sh"
"$root/scripts/build-ejs.sh"
cmake --install "$root/build" --prefix "$prefix"
if [[ -x "$root/helper/ejs" ]]; then
  mkdir -p "$prefix/lib/sung"
  cp -a -- "$root/helper/ejs" "$prefix/lib/sung/ejs"
fi
rm -f -- "$prefix/share/icons/hicolor/scalable/apps/sung.svg"
if command -v gtk-update-icon-cache >/dev/null; then gtk-update-icon-cache -f -t "$prefix/share/icons/hicolor"; fi
python3 -m venv "$prefix/lib/sung/runtime"
"$prefix/lib/sung/runtime/bin/python" -m pip install --disable-pip-version-check -r "$prefix/lib/sung/requirements.txt"
if command -v update-desktop-database >/dev/null; then update-desktop-database "$prefix/share/applications"; fi
printf 'Installed Sung to %s/bin/sung\n' "$prefix"
