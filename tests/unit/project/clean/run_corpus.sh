#!/usr/bin/env bash
# The clean-roundtrip gate over the song corpora: builds the standalone driver in the host tree (configured by
# `./dbt test unit`) and runs it on $DELUGE_SONG_CORPUS, else $DELUGE_BACKUP/SONGS, else ~/Deluge Backup/SONGS; and on
# $DELUGE_LEGACY_CORPUS too when it is set (the files as the releases wrote them, before the upgrader).
#
# Gate clean-roundtrip-corpus:
#     the model read back from each corpus song's clean save equals the model the legacy decoder read from the file,
#     but for the fields the clean load clamped (printed); a second clean save reads back the same; and every output
#     of the song, taken out as a preset, does the same (the committed synthetic songs run in the host and ARM trees)
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
tree="$repo/build/test-host"
if [[ ! -f "$tree/build.ninja" ]]; then
    echo "clean-roundtrip-corpus: configure the host tree first (./dbt test unit)" >&2
    exit 2
fi
cmake --build "$tree" --target clean_roundtrip --config Debug >/dev/null
corpus=""
for candidate in "${DELUGE_SONG_CORPUS:-}" "${DELUGE_BACKUP:+$DELUGE_BACKUP/SONGS}" "$HOME/Deluge Backup/SONGS"; do
    if [[ -n "$candidate" && -d "$candidate" ]]; then
        corpus="$candidate"
        break
    fi
done
if [[ -z "$corpus" ]]; then
    echo "clean-roundtrip-corpus: no song corpus (set DELUGE_SONG_CORPUS)" >&2
    exit 77
fi
corpora=("$corpus")
if [[ -n "${DELUGE_LEGACY_CORPUS:-}" && -d "$DELUGE_LEGACY_CORPUS" && "$DELUGE_LEGACY_CORPUS" != "$corpus" ]]; then
    corpora+=("$DELUGE_LEGACY_CORPUS")
fi
exec "$tree/unit/project/clean/Debug/clean_roundtrip" "${corpora[@]}"
