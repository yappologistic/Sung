#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cmake -S "$root" -B "$root/build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DSUNG_DIAGNOSTICS=OFF "$@"
cmake --build "$root/build" --parallel "${SUNG_BUILD_JOBS:-4}"
