#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
VCPKG_DIR="$ROOT_DIR/vcpkg"
BUILD_DIR="$ROOT_DIR/build"

if [ ! -f "$VCPKG_DIR/vcpkg" ]; then
    echo "[build] vcpkg not found. Run scripts/setup.sh first."
    exit 1
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_DIR/scripts/buildsystems/vcpkg.cmake"

make -j"$(sysctl -n hw.ncpu)"

echo ""
echo "Build complete: $BUILD_DIR/simcore_publisher"
echo "Run: $BUILD_DIR/simcore_publisher"
