#!/usr/bin/env bash
# The model-equality gate over the song corpus: builds the standalone driver in the unit tree (configured by
# `./dbt test unit`) and runs it. The corpus is found as tests/golden/driver.py finds it: $DELUGE_SONG_CORPUS, else
# $DELUGE_BACKUP/SONGS, else ~/Deluge Backup/SONGS.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
tree="$repo/build/test-unit"
if [[ ! -f "$tree/build.ninja" ]]; then
    echo "model-equality-corpus: configure the unit tree first (./dbt test unit)" >&2
    exit 2
fi
cmake --build "$tree" --target model_equality --config Debug >/dev/null
corpus=""
for candidate in "${DELUGE_SONG_CORPUS:-}" "${DELUGE_BACKUP:+$DELUGE_BACKUP/SONGS}" "$HOME/Deluge Backup/SONGS"; do
    if [[ -n "$candidate" && -d "$candidate" ]]; then
        corpus="$candidate"
        break
    fi
done
if [[ -z "$corpus" ]]; then
    echo "model-equality-corpus: no song corpus (set DELUGE_SONG_CORPUS)" >&2
    exit 77
fi
exec "$tree/model/equality/Debug/model_equality" "$corpus"
