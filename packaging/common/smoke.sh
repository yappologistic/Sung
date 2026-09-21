#!/bin/sh
# Assert an installed Sung package is actually usable.
#
# Runs in a pristine container of the packaging distribution, where none of the
# build dependencies are present -- so anything this reaches had to arrive
# through the package's declared dependencies. That is the point: Qt's QML
# modules and the ffmpeg command-line tools are invisible to every automatic
# dependency scanner, and only an install test catches a missing one.
set -eu
version="${1:?usage: smoke.sh VERSION [PREFIX]}"
prefix="${2:-/usr}"
status=0

pass() { printf 'ok    %s\n' "$1"; }
fail() { printf 'FAIL  %s\n' "$1"; status=1; }

if [ -x "$prefix/bin/sung" ]; then pass 'binary installed'; else fail 'binary installed'; fi

missing="$(ldd "$prefix/bin/sung" 2>/dev/null | grep 'not found' || true)"
if [ -z "$missing" ]; then
  pass 'shared libraries resolve'
else
  fail "shared libraries resolve ($(printf '%s' "$missing" | tr -s ' \n' ' '))"
fi

for file in \
  lib/sung/catalog.py \
  lib/sung/online_artwork.py \
  lib/sung/requirements.txt \
  share/applications/sung.desktop \
  share/icons/hicolor/512x512/apps/sung.png \
  share/licenses/sung/LICENSE
do
  if [ -f "$prefix/$file" ]; then pass "$file"; else fail "$file"; fi
done

python="$prefix/lib/sung/runtime/bin/python"
if [ -x "$python" ]; then pass 'bundled interpreter'; else fail 'bundled interpreter'; fi
if "$python" -c 'import yt_dlp, ytmusicapi' >/dev/null 2>&1; then
  pass 'bundled runtime imports yt_dlp and ytmusicapi'
else
  fail 'bundled runtime imports yt_dlp and ytmusicapi'
fi

# The helper shells out to these; a package that forgets ffmpeg still starts.
for tool in ffmpeg ffprobe node; do
  if command -v "$tool" >/dev/null 2>&1; then pass "$tool on PATH"; else fail "$tool on PATH"; fi
done

reported="$(HOME=/tmp XDG_RUNTIME_DIR=/tmp QT_QPA_PLATFORM=offscreen \
  "$prefix/bin/sung" --version 2>/dev/null || true)"
if [ "$reported" = "Sung $version" ]; then
  pass "runs offscreen and reports $version"
else
  fail "runs offscreen and reports $version (got: ${reported:-nothing})"
fi

exit "$status"
