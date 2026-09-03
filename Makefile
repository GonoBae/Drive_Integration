SHELL := /bin/sh

ifeq ($(OS),Windows_NT)
RELAY_PYTHON ?= python/relay_server/.venv/Scripts/python.exe
HOST_EXECUTABLE := ./cpp/host/build/Release/simcore_publisher.exe
BUILD_CPP_COMMAND := powershell.exe -NoProfile -ExecutionPolicy Bypass -File cpp/host/scripts/build.ps1
SETUP_CPP_COMMAND := powershell.exe -NoProfile -ExecutionPolicy Bypass -File cpp/host/scripts/setup.ps1
else
RELAY_PYTHON ?= python/relay_server/.venv/bin/python
HOST_EXECUTABLE := ./cpp/host/build/simcore_publisher
BUILD_CPP_COMMAND := cd cpp/host && bash scripts/build.sh
SETUP_CPP_COMMAND := cd cpp/host && bash scripts/setup.sh
endif

.PHONY: run-python run-cpp build-cpp setup-cpp test-cpp test-python test check-python check-proto check generate-proto

run-python:
	$(RELAY_PYTHON) -m python.relay_server

run-cpp:
	$(HOST_EXECUTABLE)

build-cpp:
	$(BUILD_CPP_COMMAND)

setup-cpp:
	$(SETUP_CPP_COMMAND)

test-cpp:
	cd cpp/host && ctest --preset release

test-python:
	$(RELAY_PYTHON) -m unittest discover -s python/relay_server/tests -t . -v

test: test-cpp

check-python: test-python

check-proto:
	$(RELAY_PYTHON) -m unittest -v python.relay_server.tests.test_generated_proto

check: test-cpp

generate-proto:
	cd python/relay_server && bash scripts/generate_proto.sh
