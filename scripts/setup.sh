#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
python3 -m venv --system-site-packages "$root/runtime"
"$root/runtime/bin/python" -m pip install --disable-pip-version-check -r "$root/helper/requirements.txt"
