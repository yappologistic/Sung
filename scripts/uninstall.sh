#!/usr/bin/env bash
set -euo pipefail
prefix="${1:-$HOME/.local}"
rm -f -- "$prefix/bin/sung" "$prefix/share/applications/sung.desktop" "$prefix/share/icons/hicolor/scalable/apps/sung.svg"
rm -rf -- "$prefix/lib/sung" "$prefix/share/licenses/sung"
if command -v update-desktop-database >/dev/null; then update-desktop-database "$prefix/share/applications"; fi
printf 'Sung removed. Your library and settings were kept.\n'
