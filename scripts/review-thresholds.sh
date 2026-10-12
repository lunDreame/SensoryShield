#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
output_file="${1:-/tmp/sensoryshield-threshold-review.json}"
test_dir=$(mktemp -d "${TMPDIR:-/tmp}/sensoryshield-threshold-review.XXXXXX")
trap 'rm -rf "$test_dir"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Itests/host/stubs -Isrc \
  tests/host/threshold_review.cpp src/common/algorithm/stimulus_score.cpp \
  src/common/algorithm/baseline.cpp src/common/algorithm/sensory_relief.cpp -o "$test_dir/review"
"$test_dir/review" > "$test_dir/result.json"
python3 - "$test_dir/result.json" "$output_file" <<'PY'
import hashlib
import json
import subprocess
import sys
from pathlib import Path

result = json.loads(Path(sys.argv[1]).read_text())
result["git_head"] = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
sources = [
    "src/definition.h", "src/common/algorithm/baseline.cpp",
    "src/common/algorithm/stimulus_score.h", "src/common/algorithm/stimulus_score.cpp",
    "src/common/algorithm/stimulus_policy.h",
    "src/common/algorithm/algorithm.cpp", "src/common/algorithm/output_aware_inputs.h",
    "src/common/algorithm/recovery_guard.h",
    "src/common/algorithm/sensory_relief.h", "src/common/algorithm/sensory_relief.cpp",
    "tests/host/fan_probe.h", "tests/host/threshold_review.cpp",
    "frontend/src/pages/onboarding/ProfileOnboarding.tsx",
]
result["source_sha256"] = {path: hashlib.sha256(Path(path).read_bytes()).hexdigest() for path in sources}
Path(sys.argv[2]).write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
PY
printf 'Synthetic threshold characterization: %s\n' "$output_file"
