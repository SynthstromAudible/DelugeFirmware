#!/usr/bin/env bash
set -eu
repo="$(cd "$(dirname "$0")/../../.." && pwd)"
out="${1:-/tmp/deluge-snake-probe}"
g++ -std=c++23 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
 -fno-sanitize-recover=all -I"$repo/src/deluge" "$repo/tests/regression/snake/probe.cpp" -o "$out"
ASAN_OPTIONS=detect_leaks=0 "$out"
