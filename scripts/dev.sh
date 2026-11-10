#!/usr/bin/env bash
# Builds everything Liora needs and runs the test suites.
#
# Some machines have a broken compiler shim earlier in PATH (for example a
# half-uninstalled Swift toolchain that answers for `cc`, `ar` and `ranlib`).
# Pinning the Xcode toolchain here keeps CMake, Rust and clippy consistent.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

if command -v xcrun >/dev/null 2>&1; then
  export CC="$(xcrun -f clang)"
  export CXX="$(xcrun -f clang++)"
  export AR="$(xcrun -f ar)"
  export RANLIB="$(xcrun -f ranlib)"
  export CC_aarch64_apple_darwin="$CC"
  export CXX_aarch64_apple_darwin="$CXX"
  export CARGO_TARGET_AARCH64_APPLE_DARWIN_LINKER="$CC"
fi

step() { printf '\n\033[1m== %s\033[0m\n' "$1"; }

step "Interface"
if [ ! -d ui/node_modules ]; then
  (cd ui && pnpm install --frozen-lockfile)
fi
(cd ui && pnpm check && pnpm test && pnpm build)

step "Core and shell"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel

step "Core tests"
ctest --test-dir build --output-on-failure

step "liora-net"
(cd net && cargo fmt --check && cargo clippy --all-targets -- -D warnings && cargo test)

printf '\n\033[1;32mAll good.\033[0m Launch with: open build/shell/liora.app\n'
