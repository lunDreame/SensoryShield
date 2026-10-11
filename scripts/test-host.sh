#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/sensoryshield-tests.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT
compiler="${CXX:-c++}"
flags=(-std=c++17 -Wall -Wextra -Werror -Itests/host/stubs -Isrc)
for test in stimulus_score light_presence sound_fan algorithm_environment memory_config environment_measurement output_aware_inputs automatic_flow occupancy_automation default_output_rebase; do
  sources=("tests/host/${test}_test.cpp")
  case "$test" in
    output_aware_inputs) sources+=(src/common/algorithm/stimulus_score.cpp src/common/algorithm/baseline.cpp) ;;
    stimulus_score) sources+=(src/common/algorithm/stimulus_score.cpp src/common/algorithm/baseline.cpp) ;;
    light_presence) sources+=(src/common/algorithm/light_presence.cpp) ;;
    sound_fan) sources+=(src/common/algorithm/stimulus_score.cpp src/common/algorithm/baseline.cpp) ;;
    algorithm_environment|automatic_flow|occupancy_automation|default_output_rebase) sources+=(src/common/algorithm/algorithm.cpp src/common/algorithm/stimulus_score.cpp src/common/algorithm/baseline.cpp src/common/algorithm/light_presence.cpp) ;;
    environment_measurement) sources+=(src/common/algorithm/stimulus_score.cpp src/common/algorithm/baseline.cpp) ;;
    memory_config) sources+=(src/system/memory.cpp) ;;
  esac
  "$compiler" "${flags[@]}" "${sources[@]}" -o "$test_dir/$test"
  "$test_dir/$test"
done
