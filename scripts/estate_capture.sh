#!/usr/bin/env bash
# Run every gate above unit level and collect what they printed, for comparing two trees.
#
#   scripts/estate_capture.sh <label>        -> .superpowers/estate/<label>/
#
# The same as `./dbt test --capture <label>`, which does the work; this name is kept because notes and plans use it.
# Writes, for each gate: <gate>.log (its whole output), and appends to markers.txt ("<gate>: <line>" for every marker
# line -- an upper-case tag followed by a space, '=', ':' or a further '-TAG' -- plus its exit status) and summary.txt
# ("<gate> rc=<n>", then the pass count). Compare two captures with:
#   diff .superpowers/estate/<a>/markers.txt .superpowers/estate/<b>/markers.txt
# Wall-time markers (maskMicros=, letGoMicros=, ...) differ run to run; everything else should not.
#
# The output lives under .superpowers/ (git-ignored) so it survives reboots, unlike a /tmp scratch directory.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
exec ./dbt test --capture "${1:?usage: scripts/estate_capture.sh <label>}"
