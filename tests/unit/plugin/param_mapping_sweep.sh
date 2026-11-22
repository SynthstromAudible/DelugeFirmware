#!/usr/bin/env bash
# Builds and runs the all-words mapping sweep (param_mapping_sweep.cpp) with the host clang.
#
# Gate param-mapping-sweep:
#     every one of the 2^32 stored words converts to the same knob position and menu number through each plugin
#     mapping kind as through the reference arithmetic kept verbatim in tests/unit/plugin/param_mapping_reference.h,
#     under UBSan; the unit-tier param_mapping spec covers the knob and menu positions exhaustively and a spread of
#     words, this covers every word
set -euo pipefail
root="$(cd "$(dirname "$0")/../../.." && pwd)"
out="${root}/build/param-mapping-sweep"
mkdir -p "${out}"
"${CXX:-clang++}" -std=c++23 -O2 -fsanitize=undefined -fno-sanitize-recover=undefined -I"${root}/src" \
	-I"${root}/tests/unit/plugin" "${root}/tests/unit/plugin/param_mapping_sweep.cpp" \
	-o "${out}/param_mapping_sweep"
"${out}/param_mapping_sweep"
