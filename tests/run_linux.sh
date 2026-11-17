#!/usr/bin/env bash
# Builds and runs the pure (EuroScope-free) unit tests with g++.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build-linux

run_test() {
	local name="$1"; shift
	g++ -std=c++20 -O1 -Wall -I. -o "build-linux/$name" "$@"
	echo "== $name"
	"./build-linux/$name"
}

run_test autoconfiguration_test tests/autoconfiguration_test.cpp autoconfiguration.cpp
run_test bridgeconfiguration_test tests/bridgeconfiguration_test.cpp autoconfiguration.cpp
run_test sidconfigv2_test tests/sidconfigv2_test.cpp sidconfigv2.cpp
run_test sidengine_test tests/sidengine_test.cpp sidengine.cpp sidconfigv2.cpp
