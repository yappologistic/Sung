#!/usr/bin/env bash
# Build the Debian package. Runs as root in debian:trixie -- trixie is the
# oldest Debian carrying the Qt 6.8 that CMakeLists.txt requires.
set -euo pipefail
tarball="$(realpath -- "${1:?usage: build.sh TARBALL OUTDIR}")"
outdir="${2:?usage: build.sh TARBALL OUTDIR}"
version="$(basename -- "$tarball" .tar.gz)"; version="${version#sung-}"

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y --no-install-recommends \
  build-essential debhelper dpkg-dev fakeroot \
  cmake ninja-build ca-certificates \
  qt6-base-dev qt6-declarative-dev qt6-declarative-dev-tools \
  qt6-multimedia-dev qt6-svg-dev \
  python3-venv python3-pip

mkdir -p -- "$outdir"
outdir="$(cd -- "$outdir" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT

tar -C "$work" -xzf "$tarball"
src="$work/sung-$version"

# The recipe travels inside the tarball; dpkg-buildpackage wants it at the
# source root as debian/.
cp -a -- "$src/packaging/debian" "$src/debian"
rm -f -- "$src/debian/build.sh" "$src/debian/verify.sh" "$src/debian/changelog.in"
sed -e "s/@VERSION@/$version/g" -e "s/@DATE@/$(date -R)/" \
  "$src/packaging/debian/changelog.in" > "$src/debian/changelog"

(cd "$src" && dpkg-buildpackage --build=binary --no-sign)

cp -- "$work"/sung_*.deb "$outdir/"
ls -l -- "$outdir"
