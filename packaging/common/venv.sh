#!/bin/sh
# Stage Sung's bundled Python runtime inside a package build root.
#
# ytmusicapi is packaged by no distribution here, so every package carries its
# own interpreter at the path backend.cpp already probes:
# <prefix>/lib/sung/runtime/bin/python.
#
# The virtualenv is created under a build root but runs from <prefix>. That
# relocation is safe for the one way Sung uses it -- the binary invokes
# runtime/bin/python directly, and a virtualenv derives sys.prefix from the
# interpreter's own path through the pyvenv.cfg beside it. Two things do not
# relocate: console-script shebangs record the creation path, so they are
# rewritten, and byte-code would bake the build root into tracebacks, so it is
# never generated.
#
# pip is deliberately left in place: it is how the README tells a user to
# refresh the YouTube resolver when an upstream change breaks playback.
#
# POSIX sh, not bash: this is the one script shared into every packaging
# container, and the Void image ships no bash.
set -eu
source_root="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
destdir="${1:?usage: venv.sh DESTDIR [PREFIX]}"
prefix="${2:-/usr}"
mkdir -p -- "$destdir"
destdir="$(cd -- "$destdir" && pwd)"
staged="$destdir$prefix/lib/sung/runtime"
installed="$prefix/lib/sung/runtime"

rm -rf -- "$staged"
mkdir -p -- "$(dirname -- "$staged")"
python3 -m venv "$staged"
"$staged/bin/python" -m pip install \
  --disable-pip-version-check --no-compile --no-warn-script-location \
  -r "$source_root/helper/requirements.txt"

find "$staged" -type d -name __pycache__ -prune -exec rm -rf -- {} +

# CPython 3.13 puts a non-ASCII alias for python in a virtualenv's bin. Package
# tools run under the C locale in CI, where bsdtar cannot encode the name and
# drops it with a warning; nothing in Sung references it.
LC_ALL=C find "$staged/bin" -mindepth 1 -maxdepth 1 -name '*[! -~]*' -exec rm -f -- {} +

# python -m venv records the path it was created at in pyvenv.cfg's command
# line and in the activate scripts. Left alone that is a build root inside a
# shipped package: rpm's check-buildroot rejects it outright, and dpkg, makepkg
# and xbps would all ship it silently broken. Rewrite it to the installed path.
escaped="$(printf '%s' "$destdir" | sed 's/[\\&|]/\\&/g')"
leaked="$(grep -rlIF -- "$destdir" "$staged" 2>/dev/null || true)"
if [ -n "$leaked" ]; then
  printf '%s\n' "$leaked" | while IFS= read -r file; do
    sed -i "s|$escaped||g" -- "$file"
  done
fi

for script in "$staged"/bin/*; do
  [ -f "$script" ] && [ ! -L "$script" ] || continue
  [ "$(head -c 2 -- "$script" 2>/dev/null || true)" = '#!' ] || continue
  sed -i "1s|^#!.*|#!$installed/bin/python|" -- "$script"
done

# Loud and local beats an opaque packaging error three steps downstream.
if grep -rlIF -- "$destdir" "$staged" >/dev/null 2>&1; then
  printf 'venv.sh: build root still referenced under %s\n' "$staged" >&2
  grep -rlIF -- "$destdir" "$staged" >&2
  exit 1
fi

printf 'staged bundled runtime at %s\n' "$installed"
