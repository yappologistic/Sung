#!/usr/bin/env bash
# Install the bundle in a pristine Fedora container and smoke-test it inside
# the sandbox. Needs --privileged for the same bubblewrap reason as the build.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
pkgdir="$(realpath -- "${1:?usage: verify.sh PKGDIR VERSION}")"
version="${2:?usage: verify.sh PKGDIR VERSION}"

appid=io.github.yappologistic.Sung

dnf install -y --setopt=install_weak_deps=False flatpak
flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
# Only the bundle is named: the runtime it needs is resolved from the repo
# recorded inside it, which is what a user's install has to do too.
flatpak install -y --noninteractive "$pkgdir"/sung-*.flatpak

# --filesystem=host is already granted, so the sandbox can read the script.
exec flatpak run --command=sh "$appid" "$here/smoke.sh" "$version"
