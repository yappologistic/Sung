#!/bin/sh
# Assertions run inside the installed Flatpak sandbox.
#
# Separate from packaging/common/smoke.sh because the layout genuinely differs:
# there is no bundled interpreter (the runtime ships Python), and the desktop
# entry and icon are renamed to the application id as Flatpak requires.
set -eu
version="${1:?usage: smoke.sh VERSION}"
status=0

pass() { printf 'ok    %s\n' "$1"; }
fail() { printf 'FAIL  %s\n' "$1"; status=1; }

appid=io.github.yappologistic.Sung

if [ -x /app/bin/sung ]; then pass 'binary installed'; else fail 'binary installed'; fi

missing="$(ldd /app/bin/sung 2>/dev/null | grep 'not found' || true)"
if [ -z "$missing" ]; then
  pass 'shared libraries resolve'
else
  fail "shared libraries resolve ($(printf '%s' "$missing" | tr -s ' \n' ' '))"
fi

for file in \
  /app/lib/sung/catalog.py \
  /app/lib/sung/online_artwork.py \
  "/app/share/applications/$appid.desktop" \
  "/app/share/icons/hicolor/512x512/apps/$appid.png" \
  "/app/share/metainfo/$appid.metainfo.xml"
do
  if [ -f "$file" ]; then pass "$file"; else fail "$file"; fi
done

# Qt reports this as the Wayland app id. It has to be the desktop entry's
# base name, which Flatpak requires to be the application id.
if grep -aqF "$appid" /app/bin/sung 2>/dev/null; then
  pass 'binary reports the application id as its desktop entry'
else
  fail 'binary reports the application id as its desktop entry'
fi

# The runtime's own interpreter, reaching the modules through PYTHONPATH.
if python3 -c 'import yt_dlp, ytmusicapi' >/dev/null 2>&1; then
  pass 'runtime python imports yt_dlp and ytmusicapi'
else
  fail 'runtime python imports yt_dlp and ytmusicapi'
fi

# ffmpeg and ffprobe come from the runtime; node is the one bundled binary.
for tool in ffmpeg ffprobe node; do
  if command -v "$tool" >/dev/null 2>&1; then pass "$tool on PATH"; else fail "$tool on PATH"; fi
done

reported="$(QT_QPA_PLATFORM=offscreen /app/bin/sung --version 2>/dev/null || true)"
if [ "$reported" = "Sung $version" ]; then
  pass "runs offscreen and reports $version"
else
  fail "runs offscreen and reports $version (got: ${reported:-nothing})"
fi

exit "$status"
