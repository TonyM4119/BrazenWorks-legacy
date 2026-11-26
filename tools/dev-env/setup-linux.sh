#!/usr/bin/env bash
# One-time Linux developer setup (Ubuntu 24.04 / Debian 13).
# Installs system packages and Rust, then writes CMakeUserPresets.json so that
# `cmake --preset dev` works. Safe to re-run.
#
#   tools/dev-env/setup-linux.sh                  # install packages + write presets
#   tools/dev-env/setup-linux.sh --skip-packages  # only write presets
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SKIP_PACKAGES=0
[[ "${1:-}" == "--skip-packages" ]] && SKIP_PACKAGES=1

step() { printf '\n\033[36m==> %s\033[0m\n' "$1"; }

if [[ $SKIP_PACKAGES -eq 0 ]]; then
  step "System packages (apt)"
  sudo apt-get update
  sudo apt-get install -y build-essential cmake ninja-build git python3 \
    qt6-base-dev qt6-svg-dev libgl-dev libtbb-dev libfontconfig-dev \
    libocct-foundation-dev libocct-modeling-data-dev libocct-modeling-algorithms-dev \
    libocct-visualization-dev libocct-data-exchange-dev libocct-ocaf-dev occt-misc

  step "Rust"
  if ! command -v cargo >/dev/null 2>&1 && [[ ! -x "$HOME/.cargo/bin/cargo" ]]; then
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal -c rustfmt -c clippy
  fi
  echo "    $("$HOME/.cargo/bin/rustc" --version 2>/dev/null || rustc --version)"
fi

step "Writing CMakeUserPresets.json"
cat > "$REPO/CMakeUserPresets.json" <<'EOF'
{
  "version": 3,
  "configurePresets": [
    { "name": "dev", "displayName": "Dev (RelWithDebInfo)", "inherits": "base",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "RelWithDebInfo" } },
    { "name": "release", "displayName": "Release", "inherits": "base",
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
echo "  ./build/dev/brazenworks"
