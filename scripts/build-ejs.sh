#!/usr/bin/env bash
# Build ahaoboy/ytdlp-ejs (SWC + embedded QuickJS) as a qjs-compatible helper.
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
commit="${SUNG_EJS_COMMIT:-03399fa26ee36c823b9fb4fc0125381e9e968733}"
src="$root/.deps/ejs/src"
out="$root/.deps/ejs/ejs"
helper="$root/helper/ejs"

need() { command -v "$1" >/dev/null || { echo "Missing command: $1 (install Rust via rustup)" >&2; exit 1; }; }
need git
need cargo

stamp="$root/.deps/ejs/commit"
if [[ -x "$out" && -f "$stamp" && "$(cat -- "$stamp")" == "$commit" ]]; then
  printf 'Using existing %s\n' "$out"
else
  mkdir -p "$(dirname "$src")"
  if [[ ! -d "$src/.git" ]]; then
    git clone https://github.com/ahaoboy/ytdlp-ejs.git "$src"
  fi
  git -C "$src" fetch origin "$commit"
  git -C "$src" checkout --force "$commit"
  # Upstream pins rquickjs via git without a rev; crates.io HEAD is now 0.14.
  python3 - "$src/Cargo.toml" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
text = p.read_text()
old = 'rquickjs = { version = "0.12", optional = true, git = "https://github.com/DelSkayn/rquickjs.git" }'
new = 'rquickjs = { version = "0.12", optional = true }'
if old not in text:
    raise SystemExit('unexpected rquickjs dependency line in ytdlp-ejs Cargo.toml')
p.write_text(text.replace(old, new, 1))
PY
  cargo_flags=(--manifest-path "$src/Cargo.toml" --release --no-default-features --features qjs)
  [[ -f "$src/Cargo.lock" ]] && cargo_flags+=(--locked)
  cargo build "${cargo_flags[@]}"
  bin="$src/target/release/ejs"
  [[ -x "$bin" ]] || bin="$src/target/release/ytdlp-ejs"
  [[ -x "$bin" ]] || { echo "ejs binary not found after cargo build" >&2; exit 1; }
  cp -a -- "$bin" "$out"
  chmod 0755 "$out"
  printf '%s\n' "$commit" >"$stamp"
fi
cp -a -- "$out" "$helper"
chmod 0755 "$helper"
printf 'Built %s\n' "$helper"
