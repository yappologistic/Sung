#!/bin/sh
# Build the Void package. Runs as root in the glibc void-linux image, which
# ships neither bash nor git -- hence POSIX sh, and hence the source tarball
# being prepared on the host rather than here.
#
# xbps-src would mean checking out and bootstrapping the whole void-packages
# tree for one template. xbps-create turns a staged install root straight into
# an .xbps carrying the same metadata, which is all a released package needs.
set -eu
tarball="$(realpath -- "${1:?usage: build.sh TARBALL OUTDIR}")"
outdir="${2:?usage: build.sh TARBALL OUTDIR}"
version="$(basename -- "$tarball" .tar.gz)"
version="${version#sung-}"

# Void images ship a stale util-linux that new libuuid/libblkid/libmount
# would break, so the system is brought forward before anything is added:
# first xbps itself, which must be current to run the second step.
xbps-install -Syu xbps
xbps-install -yu
xbps-install -y \
  cmake ninja gcc pkg-config python3 python3-pip \
  qt6-base-devel qt6-declarative-devel qt6-multimedia-devel qt6-svg-devel

mkdir -p -- "$outdir"
outdir="$(cd -- "$outdir" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT INT TERM

tar -C "$work" -xzf "$tarball"
src="$work/sung-$version"
destdir="$work/destdir"

cmake -S "$src" -B "$work/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DBUILD_TESTING=OFF \
  -DSUNG_DIAGNOSTICS=OFF
cmake --build "$work/build"
DESTDIR="$destdir" cmake --install "$work/build"
"$src/packaging/common/venv.sh" "$destdir" /usr

# Void's plain "ffmpeg" is a transitional dummy; ffmpeg6 is the real package
# shipping the ffmpeg and ffprobe binaries the catalog helper invokes.
cd "$outdir"
xbps-create \
  --architecture x86_64 \
  --pkgver "sung-${version}_1" \
  --desc 'YouTube Music, your music files, and your music server, native on Linux' \
  --maintainer 'Mohammadreza Hajianpour <hajianpour.mr@gmail.com>' \
  --license MIT \
  --homepage 'https://github.com/yappologistic/Sung' \
  --dependencies 'qt6-base>=0 qt6-declarative>=0 qt6-multimedia>=0 qt6-svg>=0 qt6-wayland>=0 qt6-imageformats>=0 python3>=0 ffmpeg6>=0 nodejs>=0' \
  "$destdir"
xbps-rindex --add ./*.xbps
ls -l -- "$outdir"
