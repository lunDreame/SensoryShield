#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
SOURCES=(src/common/algorithm/algorithm.cpp src/common/algorithm/stimulus_score.cpp src/common/algorithm/baseline.cpp src/common/algorithm/light_presence.cpp)
c++ -std=c++17 -Wall -Wextra -Werror -Itests/host/stubs -Isrc tests/host/algorithm_environment_test.cpp "${SOURCES[@]}" -o "$TEST_DIR/algorithm_environment"
"$TEST_DIR/algorithm_environment"
c++ -std=c++17 -Wall -Wextra -Werror -Itests/host/stubs -Isrc tests/host/environment_baseline_test.cpp src/system/memory.cpp "${SOURCES[@]}" -o "$TEST_DIR/environment_baseline"
"$TEST_DIR/environment_baseline"
frontend/node_modules/.bin/esbuild frontend/src/features/measure-environment/measureEnvironment.ts --bundle --platform=node --format=esm --outfile="$TEST_DIR/measurement.mjs"
node tests/ui/environment-measurement.test.mjs "$TEST_DIR/measurement.mjs"
node tests/ui/environment-baseline-api.test.mjs
