#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cmake -S "$root" -B "$root/build-tests" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DSUNG_DIAGNOSTICS=OFF
cmake --build "$root/build-tests" --parallel "${SUNG_BUILD_JOBS:-4}"
QT_QPA_PLATFORMTHEME=generic ctest --test-dir "$root/build-tests" --output-on-failure
python3 -m unittest discover -s "$root/tests" -p 'test_*.py'
