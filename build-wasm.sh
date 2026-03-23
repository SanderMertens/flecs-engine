#!/usr/bin/env sh
set -eu

EMSDK_DIR="${EMSDK_DIR:-$HOME/GitHub/emsdk}"
BUILD_DIR="build-wasm"

if [ ! -f "$EMSDK_DIR/emsdk_env.sh" ]; then
  echo "emsdk not found at $EMSDK_DIR"
  echo "Set EMSDK_DIR to point to your emsdk installation."
  exit 1
fi

# shellcheck disable=SC1091
. "$EMSDK_DIR/emsdk_env.sh" > /dev/null 2>&1

echo "Configuring wasm build..."
emcmake cmake -S . -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release

echo "Building..."
cmake --build "$BUILD_DIR" --config Release --parallel 8

echo "Build complete."
