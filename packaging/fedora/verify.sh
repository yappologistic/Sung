#!/usr/bin/env bash
# Install the built package in a pristine Fedora container and smoke-test it.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
pkgdir="$(realpath -- "${1:?usage: verify.sh PKGDIR VERSION}")"
version="${2:?usage: verify.sh PKGDIR VERSION}"

# dnf resolves the package's own Requires; nothing is pre-seeded, so a missing
# one surfaces here rather than on a user's machine.
dnf install -y --setopt=install_weak_deps=False "$pkgdir"/sung-*.rpm
exec "$here/../common/smoke.sh" "$version"
