#!/bin/sh
# Install the built package in a pristine Void container and smoke-test it.
set -eu
here="$(cd -- "$(dirname -- "$0")" && pwd)"
pkgdir="$(realpath -- "${1:?usage: verify.sh PKGDIR VERSION}")"
version="${2:?usage: verify.sh PKGDIR VERSION}"

# Same staged update as the build: a Void container cannot take new packages
# until its own base is current.
xbps-install -Syu xbps
xbps-install -yu
# Resolved out of the local repository the build indexed, so the package's own
# dependencies are what pull Qt, ffmpeg and node in.
xbps-install -y --repository="$pkgdir" sung
exec "$here/../common/smoke.sh" "$version"
