#!/usr/bin/env bash
set -eu
repo="$(cd "$(dirname "$0")/../../.." && pwd)"
out="${1:-/tmp/deluge-plaits-probe}"
g++ -std=c++23 -O1 -g -DTEST -fsanitize=address,undefined -fno-sanitize-recover=all \
 -I"$repo/src/deluge" -I"$repo/src/plaits_vendor" \
 "$repo/tests/regression/plaits/probe.cpp" "$repo"/src/plaits_vendor/plaits/dsp/engine/*.cc \
 "$repo/src/plaits_vendor/plaits/resources.cc" "$repo/src/plaits_vendor/stmlib/dsp/units.cc" -o "$out"
ASAN_OPTIONS=detect_leaks=0 "$out" /tmp/plaits-demo.pcm

g++ -std=c++23 -O1 -g -DTEST -fsanitize=address,undefined -fno-sanitize-recover=all \
 -I"$repo/src/plaits_vendor" "$repo/tests/regression/plaits/aux_probe.cpp" \
 "$repo"/src/plaits_vendor/plaits/dsp/engine/*.cc "$repo/src/plaits_vendor/plaits/resources.cc" \
 "$repo/src/plaits_vendor/stmlib/dsp/units.cc" -o "$out-aux"
ASAN_OPTIONS=detect_leaks=0 "$out-aux"

g++ -std=c++23 -O1 -g -DTEST -fsanitize=address,undefined -fno-sanitize-recover=all \
 -I"$repo/src/deluge" -I"$repo/src/plaits_vendor" \
 "$repo/tests/regression/plaits/rc2_probe.cpp" "$repo"/src/plaits_vendor/plaits/dsp/engine/*.cc \
 "$repo/src/plaits_vendor/plaits/resources.cc" "$repo/src/plaits_vendor/stmlib/dsp/units.cc" -o "$out-rc2"
ASAN_OPTIONS=detect_leaks=0 "$out-rc2"
