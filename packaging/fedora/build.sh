#!/usr/bin/env bash
# Build the Fedora package. Runs as root in fedora:42.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
tarball="$(realpath -- "${1:?usage: build.sh TARBALL OUTDIR}")"
outdir="${2:?usage: build.sh TARBALL OUTDIR}"
version="$(basename -- "$tarball" .tar.gz)"; version="${version#sung-}"

dnf install -y --setopt=install_weak_deps=False \
  rpm-build rpmdevtools cmake ninja-build gcc-c++ \
  qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtmultimedia-devel qt6-qtsvg-devel \
  python3-pip

mkdir -p -- "$outdir"
outdir="$(cd -- "$outdir" && pwd)"
work="$(mktemp -d)"
trap 'rm -rf -- "$work"' EXIT

mkdir -p "$work"/{SOURCES,SPECS,BUILD,BUILDROOT,RPMS,SRPMS}
cp -- "$tarball" "$work/SOURCES/"
sed "s/@VERSION@/$version/g" "$here/sung.spec.in" > "$work/SPECS/sung.spec"

rpmbuild --define "_topdir $work" -bb "$work/SPECS/sung.spec"

cp -- "$work"/RPMS/*/sung-*.rpm "$outdir/"
ls -l -- "$outdir"
