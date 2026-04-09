#!/bin/bash
# proto 파일로부터 Python 코드를 생성합니다.
# 실행: bash scripts/generate_proto.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

cd "$ROOT_DIR"

python3 -m grpc_tools.protoc \
    -I proto \
    --python_out=generated \
    proto/vehicle.proto

echo "generated/vehicle_pb2.py 생성 완료"
