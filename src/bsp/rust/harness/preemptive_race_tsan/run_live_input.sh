#!/usr/bin/env bash
# The cross-tier race meter over live input: run.sh with a song whose two synths play the left input pitch-shifted,
# in short notes, so their live histories go unread and are read again. See make_live_input_song.py.
#
# Usage: ./run_live_input.sh              # build + 3 runs, 3000 blocks
#        SKIP_BUILD=1 NUM_RUNS=5 ./run_live_input.sh
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
GOLDEN_DIR="${LIVE_INPUT_GOLDEN_DIR:-$SCRIPT_DIR/logs/live_input_golden}"
rm -rf "$GOLDEN_DIR/liveinput_proj"
python3 "$SCRIPT_DIR/make_live_input_song.py" "$REPO_ROOT/tests/fixtures/songs/two_synths_one_id.XML" \
    "$GOLDEN_DIR/liveinput_proj"
DELUGE_GOLDEN_DIR="$GOLDEN_DIR" \
DELUGE_STREAMING_SCENARIO_FIXTURE=liveinput \
DELUGE_STREAMING_SCENARIO_SONG=SONGS/LiveInput.XML \
NUM_RUNS="${NUM_RUNS:-3}" BLOCKS="${BLOCKS:-3000}" \
LOG_DIR="${LOG_DIR:-$SCRIPT_DIR/logs/live_input}" \
    exec "$SCRIPT_DIR/run.sh"
