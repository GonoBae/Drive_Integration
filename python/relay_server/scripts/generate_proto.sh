#!/bin/bash
# proto 파일로부터 Python 코드를 생성합니다.
# 실행: bash scripts/generate_proto.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RELAY_DIR="$(dirname "$SCRIPT_DIR")"
REPOSITORY_DIR="$(cd "$RELAY_DIR/../.." && pwd)"
PROTOCOL_DIR="$REPOSITORY_DIR/protocol"
GENERATED_DIR="$RELAY_DIR/generated"
VENV_PYTHON="$RELAY_DIR/.venv/bin/python"

mkdir -p "$GENERATED_DIR"

if [ -x "$VENV_PYTHON" ]; then
    PYTHON="$VENV_PYTHON"
else
    PYTHON="python3"
fi

"$PYTHON" -m grpc_tools.protoc \
    -I "$PROTOCOL_DIR" \
    --python_out="$GENERATED_DIR" \
    "$PROTOCOL_DIR/vehicle.proto"

echo "$GENERATED_DIR/vehicle_pb2.py 생성 완료"
