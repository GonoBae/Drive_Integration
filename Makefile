SHELL := /bin/sh

.PHONY: run-python run-cpp build-cpp setup-cpp test-cpp generate-proto

run-python:
	cd python/relay_server && . .venv/bin/activate && uvicorn main:app --host 0.0.0.0 --port 8000 --reload

run-cpp:
	./cpp/host/build/simcore_publisher

build-cpp:
	cd cpp/host && bash scripts/build.sh

setup-cpp:
	cd cpp/host && bash scripts/setup.sh

test-cpp:
	cd cpp/host && ctest --preset release

generate-proto:
	cd python/relay_server && bash scripts/generate_proto.sh
