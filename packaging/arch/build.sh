#!/usr/bin/env bash
# Build the Arch package. Runs as root in archlinux:base-devel.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
tarball="$(realpath -- "${1:?usage: build.sh TARBALL OUTDIR}")"
outdir="${2:?usage: build.sh TARBALL OUTDIR}"
version="$(basename -- "$tarball" .tar.gz)"; version="${version#sung-}"

pacman -Syu --noconfirm --needed \
  cmake ninja python python-pip \
  qt6-base qt6-declarative qt6-multimedia qt6-svg qt6-wayland qt6-imageformats \
  ffmpeg nodejs

mkdir -p -- "$outdir"
outdir="$(cd -- "$outdir" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT

sed "s/@VERSION@/$version/g" "$here/PKGBUILD.in" > "$work/PKGBUILD"
cp -- "$tarball" "$work/"

# makepkg refuses to run as root, by design.
useradd --create-home --no-user-group --gid users builder 2>/dev/null || true
chown -R builder "$work"
# Dependencies are already installed above, so makepkg only has to verify them.
su builder -s /bin/bash -c "cd '$work' && makepkg --noconfirm"

cp -- "$work"/*.pkg.tar.zst "$outdir/"
ls -l -- "$outdir"
