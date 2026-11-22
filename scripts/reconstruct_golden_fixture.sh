#!/usr/bin/env bash
#
# Rebuild a golden fixture's project tree (SONGS/…, SAMPLES/…) from the Deluge backup corpus into
# $DELUGE_GOLDEN_DIR/<fixture>_proj. src/platform/device/src/storage/sd_image.rs runs this when a scenario's fixture tree
# is absent.
#
# Env:
#   FIXTURE             cordae | highsiderr | icoustic    (default: cordae)
#   DELUGE_GOLDEN_DIR   where the project tree goes        (default: ~/.cache/deluge-golden)
#   DELUGE_BACKUP       the "Deluge Backup" root           (default: ~/Deluge Backup)
#
set -euo pipefail

GOLDEN_DIR="${DELUGE_GOLDEN_DIR:-$HOME/.cache/deluge-golden}"
BACKUP="${DELUGE_BACKUP:-$HOME/Deluge Backup}"

FIXTURE="${FIXTURE:-cordae}"
case "$FIXTURE" in
cordae)     SONG_DIR="$BACKUP/SONGS/problem_songs/Cordae";                 SONG_XML="Cordae.XML" ;;
highsiderr) SONG_DIR="$BACKUP/SONGS/problem_songs/highsiderr_drum_stealing"; SONG_XML="06 Ilove Techno Song_Final_2.XML" ;;
icoustic)   SONG_DIR="$BACKUP/SONGS/problem_songs/IcousticFeb19";          SONG_XML="Grunnmur 58 Nobass 14 Comm-Test.XML" ;;
*) echo "unknown FIXTURE='$FIXTURE' (cordae|highsiderr|icoustic)"; exit 2 ;;
esac

PROJ="$GOLDEN_DIR/${FIXTURE}_proj"

# Rebuild the fixture's project tree from the backup. The backup stores the song's samples in a
# flat folder next to the song, with '/' replaced by '_' in the names (e.g. SAMPLES/DRUMS/Kick/
# Kick 21.wav -> Cordae/DRUMS_Kick_Kick 21.wav); reverse that into a normal SD-card tree.
reconstruct_project() {
	[ -f "$SONG_DIR/$SONG_XML" ] || { echo "ERROR: '$SONG_DIR/$SONG_XML' not found (set DELUGE_BACKUP/FIXTURE)"; exit 2; }
	# Collected samples live in the single subfolder next to the song (mangled '/'->'_' names).
	local sub; sub="$(find "$SONG_DIR" -mindepth 1 -maxdepth 1 -type d | head -1)"
	echo "reconstructing project from '$SONG_DIR' -> $PROJ"
	rm -rf "$PROJ"; mkdir -p "$PROJ/SONGS"
	cp "$SONG_DIR/$SONG_XML" "$PROJ/SONGS/$SONG_XML"
	local ref rel mangled src ok=0 miss=0
	while IFS= read -r ref; do
		rel="${ref#SAMPLES/}"; mangled="${rel//\//_}"; src="$sub/$mangled"
		if [ -f "$src" ]; then
			mkdir -p "$PROJ/$(dirname "$ref")"; cp "$src" "$PROJ/$ref"; ok=$((ok+1))
		else
			echo "  WARN missing sample: $src"; miss=$((miss+1))
		fi
	# Both attributes matter: `fileName=` covers kit/synth sample refs, `filePath=` covers audioClip
	# recordings. Grepping only fileName= would silently drop the audio-clip refs (36 in the icoustic
	# fixture), leaving those samples absent from the reconstructed project — quietly shrinking what
	# the golden render actually exercises.
	done < <(grep -hoE '(fileName|filePath)="[^"]*"' "$SONG_DIR/$SONG_XML" \
		| sed -E 's/^(fileName|filePath)="//; s/"$//' | sort -u)
	echo "  samples: $ok copied, $miss missing"
}

reconstruct_project
