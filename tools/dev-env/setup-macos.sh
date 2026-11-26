#!/usr/bin/env bash
# One-time macOS developer setup (Homebrew). Installs dependencies and Rust,
# then writes CMakeUserPresets.json so that `cmake --preset dev` works.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
step() { printf '\n\033[36m==> %s\033[0m\n' "$1"; }

command -v brew >/dev/null 2>&1 || { echo "error: install Homebrew first: https://brew.sh"; exit 1; }
xcode-select -p >/dev/null 2>&1 || { echo "error: run 'xcode-select --install' first"; exit 1; }

step "Homebrew packages"
brew install cmake ninja git qt opencascade

step "Rust"
if ! command -v cargo >/dev/null 2>&1 && [[ ! -x "$HOME/.cargo/bin/cargo" ]]; then
  curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal -c rustfmt -c clippy
fi

PREFIX="$(brew --prefix qt);$(brew --prefix opencascade)"

step "Writing CMakeUserPresets.json"
cat > "$REPO/CMakeUserPresets.json" <<EOF
{
  "version": 3,
  "configurePresets": [
    { "name": "dev", "displayName": "Dev (RelWithDebInfo)", "inherits": "base",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "RelWithDebInfo", "CMAKE_PREFIX_PATH": "$PREFIX" } },
    { "name": "release", "displayName": "Release", "inherits": "dev",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" } }
  ],
  "buildPresets": [
    { "name": "dev", "configurePreset": "dev" },
    { "name": "release", "configurePreset": "release" }
  ],
  "testPresets": [
    { "name": "dev", "configurePreset": "dev", "output": { "outputOnFailure": true } },
    { "name": "release", "configurePreset": "release", "output": { "outputOnFailure": true } }
  ]
}
EOF
echo "    $REPO/CMakeUserPresets.json"

printf '\n\033[32mDone. Build and run:\033[0m\n'
echo "  cmake --preset dev && cmake --build --preset dev && ctest --preset dev"
echo "  open build/dev/brazenworks.app"
