#!/usr/bin/env bash
# Builds and runs the local Kitesurf engine that Liora's "kitesurf" kernel
# connects to (ws://127.0.0.1:8787/devtools/browser by default).
#
# Kitesurf is experimental and NOT a hardened sandbox: do not open untrusted
# sites with it. See third_party/kitesurf/README.md.
#
# Requirements: Node 22+, pnpm (corepack pnpm works), bazelisk as `bazel`,
# GNU tar as `gtar` (brew install gnu-tar). The first build compiles the Rust
# core and takes a while; later builds reuse Bazel's cache.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE="$ROOT/third_party/kitesurf"

if [ ! -d "$ENGINE" ]; then
  echo "Kitesurf source not found. Clone it first:" >&2
  echo "  git clone --depth 1 https://github.com/cloudflare/kitesurf.git third_party/kitesurf" >&2
  exit 1
fi

# bazelisk resolves "latest" through a Google endpoint that some networks
# block; pin the version the engine's .bazelversion asks for.
if [ -f "$ENGINE/.bazelversion" ]; then
  export USE_BAZEL_VERSION="$(cat "$ENGINE/.bazelversion")"
fi

# GNU tar is required on macOS; keep it ahead of the BSD tar.
if [ -d /opt/homebrew/opt/gnu-tar/libexec/gnubin ]; then
  export PATH="/opt/homebrew/opt/gnu-tar/libexec/gnubin:$PATH"
fi

PNPM=pnpm
if ! command -v pnpm >/dev/null 2>&1; then
  if command -v corepack >/dev/null 2>&1; then
    PNPM="corepack pnpm"
  else
    echo "pnpm not found. Install pnpm 9 or enable corepack." >&2
    exit 1
  fi
fi

cd "$ENGINE"
if [ ! -d node_modules ]; then
  $PNPM install --frozen-lockfile
fi

export KITESURF_PORT="${KITESURF_PORT:-8787}"
echo "Starting Kitesurf on http://127.0.0.1:$KITESURF_PORT (CDP: ws://127.0.0.1:$KITESURF_PORT/devtools/browser)"
exec $PNPM run dev