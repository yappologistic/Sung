#!/usr/bin/env bash
# Build the release tarball every distribution recipe consumes.
#
# Runs on the host, where git exists: the distribution images are minimal and
# several ship no git at all. Tracked files only, so build output, the local
# runtime and personal media stay out exactly as the allowlist intends. Staged
# files are included, so an uncommitted recipe can be test-built after git add.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
outdir="${1:?usage: tarball.sh OUTDIR}"
version="$("$root/scripts/version.sh")"

mkdir -p -- "$outdir"
outdir="$(cd -- "$outdir" && pwd)"
tarball="$outdir/sung-$version.tar.gz"

git -C "$root" ls-files -z |
  tar -C "$root" --null --files-from - \
      --transform "s,^,sung-$version/," --owner=0 --group=0 \
      --mode='go-w' -czf "$tarball"

printf '%s\n' "$tarball"
