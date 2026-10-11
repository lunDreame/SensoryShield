#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
output_dir="${1:-/tmp/sensoryshield-environment-simulation}"
mkdir -p "$output_dir"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Isrc tests/host/environment_simulation.cpp src/common/algorithm/stimulus_score.cpp src/common/algorithm/baseline.cpp src/common/algorithm/light_presence.cpp -o "$output_dir/simulate"
for scenario in daylight-slow daylight-fast dim-restore; do
  for learning in learn freeze; do
    "$output_dir/simulate" "$scenario" "$learning" 0 > "$output_dir/$scenario-$learning.csv"
  done
done
for preferred in 100 60; do
  for coupling in 0 0.05 0.1 0.25 0.5 1 2; do
    for learning in learn freeze; do
      "$output_dir/simulate" closed-loop "$learning" "$coupling" "$preferred" > "$output_dir/closed-loop-p$preferred-k$coupling-$learning.csv"
    done
  done
  for coupling in 0 0.5; do
    for learning in learn freeze; do
      "$output_dir/simulate" manual-return "$learning" "$coupling" "$preferred" > "$output_dir/manual-return-p$preferred-k$coupling-$learning.csv"
    done
  done
  for coupling in 0 0.25 0.5 1 2; do
    "$output_dir/simulate" closed-restore learn "$coupling" "$preferred" > "$output_dir/closed-restore-p$preferred-k$coupling-learn.csv"
  done
  for learning in learn freeze; do
    "$output_dir/simulate" closed-loop "$learning" 0.5 "$preferred" 1 100 > "$output_dir/closed-loop-p$preferred-k0.5-phase100-$learning.csv"
  done
done
printf 'Simulation CSVs: %s\n' "$output_dir"
