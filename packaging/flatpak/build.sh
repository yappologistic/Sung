#!/usr/bin/env bash
# Build the Flatpak bundle. Runs as root in fedora:42, which needs --privileged
# because bubblewrap has to construct the build sandbox inside the container.
set -euo pipefail
tarball="$(realpath -- "${1:?usage: build.sh TARBALL OUTDIR}")"
outdir="${2:?usage: build.sh TARBALL OUTDIR}"
version="$(basename -- "$tarball" .tar.gz)"; version="${version#sung-}"

appid=io.github.yappologistic.Sung
runtime_version=6.11
flathub=https://dl.flathub.org/repo/flathub.flatpakrepo

dnf install -y --setopt=install_weak_deps=False flatpak flatpak-builder xz
flatpak remote-add --if-not-exists flathub "$flathub"
flatpak install -y --noninteractive flathub \
  "org.kde.Platform//$runtime_version" "org.kde.Sdk//$runtime_version"

mkdir -p -- "$outdir"
outdir="$(cd -- "$outdir" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT

tar -C "$work" -xzf "$tarball"
src="$work/sung-$version"

# rofiles-fuse needs /dev/fuse, which a container generally will not have.
# The state dir has to sit on the same filesystem as the build dir, and its
# default is the working directory -- which here is a bind mount of the
# checkout, so it is pointed at the work tree instead.
flatpak-builder --disable-rofiles-fuse --force-clean \
  --state-dir="$work/state" \
  --repo="$work/repo" "$work/build" \
  "$src/packaging/flatpak/$appid.yml"

# The runtime repo is recorded in the bundle so installing it on a machine with
# no flathub remote still knows where to fetch org.kde.Platform from.
flatpak build-bundle --runtime-repo="$flathub" \
  "$work/repo" "$outdir/sung-$version.flatpak" "$appid"

ls -l -- "$outdir"
