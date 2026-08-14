#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
VCPKG_DIR="$ROOT_DIR/vcpkg"

if [ ! -f "$VCPKG_DIR/vcpkg" ]; then
    echo "[build] vcpkg not found. Run scripts/setup.sh first."
    exit 1
fi

cd "$ROOT_DIR"

cmake --preset release
cmake --build --preset release

echo ""
echo "Build complete: $ROOT_DIR/build/simcore_publisher"
echo "Run: $ROOT_DIR/build/simcore_publisher"
