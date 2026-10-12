#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
g++ -std=c++23 -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize=address,undefined -Isrc/deluge tests/regression/mdd_features/tie_overlap.cpp -o /tmp/mdd-features-tie-overlap
ASAN_OPTIONS=detect_leaks=0 /tmp/mdd-features-tie-overlap
python3 tests/regression/mdd_features/baked_source_probe.py
python3 tests/regression/mdd_features/automation_range_probe.py
python3 tests/regression/mdd/write_probe.py
python3 tests/regression/snake/target_probe.py
