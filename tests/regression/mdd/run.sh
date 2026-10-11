#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../../.."
python3 tests/regression/mdd/custom_signal_probe.py
python3 tests/regression/mdd/depth_probe.py
python3 tests/regression/mdd/scale_button_probe.py
python3 tests/regression/mdd/scroll_probe.py
python3 tests/regression/mdd/song_scale_probe.py
python3 tests/regression/mdd/write_probe.py
python3 tests/regression/mdd/native_menu_probe.py
python3 tests/regression/mdd/sync_probe.py
python3 tests/regression/mdd/grid_probe.py
python3 tests/regression/mdd/playback_probe.py
python3 tests/regression/mdd/view_probe.py
for name in core_probe codec_probe playback_probe engine_probe bake_probe startup_probe; do
 g++ -std=c++23 -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize=undefined -D_GLIBCXX_DEBUG -Isrc/deluge "tests/regression/mdd/$name.cpp" -o "/tmp/mdd-$name"
 "/tmp/mdd-$name"
done
