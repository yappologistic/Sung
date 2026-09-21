#!/usr/bin/env bash
# The version in CMakeLists.txt is the single source of truth. Packaging
# recipes carry @VERSION@ and are instantiated from this, so a release means
# editing one line of one file.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
version="$(sed -n 's/^project(Sung VERSION \([0-9][0-9.]*\).*/\1/p' "$root/CMakeLists.txt")"
if [ -z "$version" ]; then
  printf 'no project(Sung VERSION ...) in CMakeLists.txt\n' >&2
  exit 1
fi
printf '%s\n' "$version"
