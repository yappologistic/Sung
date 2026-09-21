#!/usr/bin/env bash
# Install the built package in a pristine Arch container and smoke-test it.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
pkgdir="${1:?usage: verify.sh PKGDIR VERSION}"
version="${2:?usage: verify.sh PKGDIR VERSION}"

pacman -Syu --noconfirm
pacman -U --noconfirm -- "$pkgdir"/*.pkg.tar.zst
exec "$here/../common/smoke.sh" "$version"
