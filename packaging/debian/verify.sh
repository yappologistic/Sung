#!/usr/bin/env bash
# Install the built package in a pristine Debian container and smoke-test it.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
pkgdir="$(realpath -- "${1:?usage: verify.sh PKGDIR VERSION}")"
version="${2:?usage: verify.sh PKGDIR VERSION}"

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
# apt resolves the package's own declared dependencies; nothing is pre-seeded,
# so a missing Depends surfaces here rather than on a user's machine.
apt-get install -y --no-install-recommends "$pkgdir"/sung_*.deb
exec "$here/../common/smoke.sh" "$version"
