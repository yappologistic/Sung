#!/usr/bin/env bash
# Full verification: deterministic tests, live UI/playback, visual captures, MPRIS.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
exec python3 "$root/tests/verify.py" "$@"
