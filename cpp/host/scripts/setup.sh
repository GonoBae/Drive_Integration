#!/bin/bash
# vcpkg를 프로젝트 내부에 클론하고 초기화합니다.
# 최초 1회만 실행하면 됩니다.
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
VCPKG_DIR="$ROOT_DIR/vcpkg"

if [ -d "$VCPKG_DIR" ]; then
    echo "[setup] vcpkg already exists, skipping clone."
else
    echo "[setup] Cloning vcpkg..."
    git clone https://github.com/microsoft/vcpkg.git "$VCPKG_DIR"
fi

echo "[setup] Bootstrapping vcpkg..."
"$VCPKG_DIR/bootstrap-vcpkg.sh" -disableMetrics

echo ""
echo "[setup] Done. vcpkg is ready at: $VCPKG_DIR"
echo "Next: bash scripts/build.sh"
