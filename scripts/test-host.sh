#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/sensoryshield-tests.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT
compiler="${CXX:-c++}"
flags=(-std=c++17 -pthread -Wall -Wextra -Werror -Itests/host/stubs -Isrc)
"$compiler" -Itests/host/mic_stubs "${flags[@]}" tests/host/mic_capture_test.cpp src/peripheral/Mic.cpp -o "$test_dir/mic_capture"
"$test_dir/mic_capture"
for test in stimulus_score light_presence sound_fan algorithm_environment memory_config environment_measurement output_aware_inputs fan_peripheral automatic_flow occupancy_automation default_output_rebase proportional_flow joint_relief fan_output_integration system_runtime; do
  sources=("tests/host/${test}_test.cpp")
  case "$test" in
    output_aware_inputs) sources+=(src/common/algorithm/stimulus_score.cpp src/common/algorithm/baseline.cpp) ;;
    stimulus_score) sources+=(src/common/algorithm/stimulus_score.cpp src/common/algorithm/baseline.cpp) ;;
    light_presence) sources+=(src/common/algorithm/sensory_relief.cpp) ;;
    sound_fan) sources+=(src/common/algorithm/sensory_relief.cpp src/common/algorithm/stimulus_score.cpp src/common/algorithm/baseline.cpp) ;;
    algorithm_environment|automatic_flow|occupancy_automation|default_output_rebase|proportional_flow|joint_relief) sources+=(src/common/algorithm/algorithm.cpp src/common/algorithm/stimulus_score.cpp src/common/algorithm/baseline.cpp src/common/algorithm/sensory_relief.cpp) ;;
    environment_measurement) sources+=(src/common/algorithm/sensory_relief.cpp src/common/algorithm/stimulus_score.cpp src/common/algorithm/baseline.cpp) ;;
    system_runtime) sources+=(src/system/system.cpp src/system/memory.cpp src/common/algorithm/algorithm.cpp src/common/algorithm/baseline.cpp src/common/algorithm/stimulus_score.cpp src/common/algorithm/sensory_relief.cpp src/common/device/device.cpp src/common/device/Light.cpp src/common/device/Fan.cpp src/common/device/Occupancy.cpp src/common/device/Illuminance.cpp src/peripheral/Fan.cpp src/peripheral/PIR.cpp) ;;
    fan_output_integration) sources+=(src/common/algorithm/algorithm.cpp src/common/algorithm/stimulus_score.cpp src/common/algorithm/baseline.cpp src/common/algorithm/sensory_relief.cpp src/peripheral/Fan.cpp) ;;
    fan_peripheral) sources+=(src/peripheral/Fan.cpp) ;;
    memory_config) sources+=(src/system/memory.cpp) ;;
  esac
  "$compiler" "${flags[@]}" -DSENSORYSHIELD_DEMO_DEFAULTS=0 "${sources[@]}" -o "$test_dir/$test"
  "$test_dir/$test"
done

# Verify demo timing separately; the suite above covers everyday proportional control.
for test in rehearsal_flow occupancy_automation system_runtime; do
  if [[ "$test" != system_runtime ]]; then
    sources=("tests/host/${test}_test.cpp" src/common/algorithm/algorithm.cpp src/common/algorithm/stimulus_score.cpp src/common/algorithm/baseline.cpp src/common/algorithm/sensory_relief.cpp)
  else
    sources=(tests/host/system_runtime_test.cpp src/system/system.cpp src/system/memory.cpp src/common/algorithm/algorithm.cpp src/common/algorithm/baseline.cpp src/common/algorithm/stimulus_score.cpp src/common/algorithm/sensory_relief.cpp src/common/device/device.cpp src/common/device/Light.cpp src/common/device/Fan.cpp src/common/device/Occupancy.cpp src/common/device/Illuminance.cpp src/peripheral/Fan.cpp src/peripheral/PIR.cpp)
  fi
  "$compiler" "${flags[@]}" "${sources[@]}" -o "$test_dir/demo-$test"
  "$test_dir/demo-$test"
done

# Stored settings still take precedence in both profiles.
for profile in 1 0; do
  "$compiler" "${flags[@]}" -DSENSORYSHIELD_DEMO_DEFAULTS="$profile" tests/host/default_profiles_test.cpp src/system/memory.cpp src/common/algorithm/algorithm.cpp src/common/algorithm/stimulus_score.cpp src/common/algorithm/baseline.cpp src/common/algorithm/sensory_relief.cpp -o "$test_dir/default-profile-$profile"
  "$test_dir/default-profile-$profile"
done
