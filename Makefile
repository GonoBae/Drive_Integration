SHELL := /bin/zsh

.PHONY: run-python run-cpp build-cpp setup-cpp

run-python:
	cd python/relay_server && source .venv/bin/activate && uvicorn main:app --host 0.0.0.0 --port 8000 --reload

run-cpp:
	./cpp/host/build/simcore_publisher

build-cpp:
	cd cpp/host && bash scripts/build.sh

setup-cpp:
	cd cpp/host && bash scripts/setup.sh