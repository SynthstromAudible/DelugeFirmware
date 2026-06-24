#!/usr/bin/env bash
#
# Golden A/B harness for the headless Cordae render (deluge_render).
#
# Renders the Cordae song deterministically and byte-compares the output stem against a
# stored golden WAV. Intended for A/B-ing allocator / resource-manager (or any render-path)
# changes: capture a golden from a known-good build, then `check` after each change.
#
# The render is bit-reproducible (DELUGE_HOST_DETERMINISTIC + DELUGE_DETERMINISTIC_ALLOC),
# so the comparison is exact. Defaults to MIXDOWN (the master); set MODE=TRACK for per-track.
#
# Usage:
#   scripts/golden_mixdown.sh update        # render and save as the golden reference (the "A")
#   scripts/golden_mixdown.sh check         # render and diff against the golden (the "B"); default
#   scripts/golden_mixdown.sh padsweep      # render at several heap-pad layouts; assert all bit-identical
#   scripts/golden_mixdown.sh reconstruct   # (re)build the Cordae project tree from the backup
#
# `padsweep` is the layout-invariance guard: it proves the render depends on no allocation address (no
# overread, no layout-coupled PRNG draw count). It compares renders to EACH OTHER, so unlike `check` it
# catches layout fragility even when the golden matches a fixed layout. Run it after any memory/allocator
# or manager-struct change.
#
# Env overrides:
#   DELUGE_GOLDEN_DIR   where the project + golden live   (default: ~/.cache/deluge-golden)
#   DELUGE_BACKUP       the "Deluge Backup" root           (default: ~/Deluge Backup)
#   BUILD_DIR           the sim build dir                  (default: <repo>/build-sim-cpp)
#   MODE                MIXDOWN | TRACK | CLIP | DRUM      (default: MIXDOWN)
#   SONG                song path within the project       (default: SONGS/Cordae.XML)
#   PADS                pad sizes for `padsweep`           (default: "16 64 256 4096")
#   PAD                 one-off DELUGE_SIM_HEAP_PAD value for check/update (default: 0)
#   NO_BUILD=1          skip the deluge_render rebuild
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO/build-sim-cpp}"
GOLDEN_DIR="${DELUGE_GOLDEN_DIR:-$HOME/.cache/deluge-golden}"
BACKUP="${DELUGE_BACKUP:-$HOME/Deluge Backup}"
MODE="${MODE:-MIXDOWN}"
CMD="${1:-check}"

# Fixture = which backup song to render. Cordae is the default (no cache use — the
# raw-cluster A/B); the others exercise the derived caches (P3): `highsiderr` makes
# ~94 SampleCache (repitch) clusters, `icoustic` exercises the perc (time-stretch) cache.
FIXTURE="${FIXTURE:-cordae}"
case "$FIXTURE" in
cordae)     SONG_DIR="$BACKUP/SONGS/problem_songs/Cordae";                 SONG_XML="Cordae.XML" ;;
highsiderr) SONG_DIR="$BACKUP/SONGS/problem_songs/highsiderr_drum_stealing"; SONG_XML="06 Ilove Techno Song_Final_2.XML" ;;
icoustic)   SONG_DIR="$BACKUP/SONGS/problem_songs/IcousticFeb19";          SONG_XML="Grunnmur 58 Nobass 14 Comm-Test.XML" ;;
*) echo "unknown FIXTURE='$FIXTURE' (cordae|highsiderr|icoustic)"; exit 2 ;;
esac
SONG="${SONG:-SONGS/$SONG_XML}"

PROJ="$GOLDEN_DIR/${FIXTURE}_proj"
GOLDEN="$GOLDEN_DIR/${FIXTURE}_${MODE}.golden.wav"
RENDER="$BUILD_DIR/deluge_render"

# Rebuild the Cordae project tree from the backup. The backup stores the song's samples in a
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
	done < <(grep -ho 'fileName="[^"]*"' "$SONG_DIR/$SONG_XML" | sed 's/fileName="//;s/"$//' | sort -u)
	echo "  samples: $ok copied, $miss missing"
}

build() { [ "${NO_BUILD:-0}" = 1 ] || cmake --build "$BUILD_DIR" --target deluge_render >/dev/null; }

# Render into $1; echo the produced WAV path (empty on failure). Honours PAD (the sim-only
# DELUGE_SIM_HEAP_PAD knob — leaked SDRAM bytes that shift allocation layout; default 0 = off).
render() {
	local out="$1"; rm -rf "$out"
	DELUGE_HOST_DETERMINISTIC=1 DELUGE_SIM_HEAP_PAD="${PAD:-0}" \
		"$RENDER" --project "$PROJ" --song "$SONG" --mode "$MODE" --out "$out" \
		>"$out.log" 2>&1 || true
	find "$out" -name '*.WAV' 2>/dev/null | sort | head -1
}

# Audio-level diff on mismatch (best-effort; needs python3).
audio_diff() {
	command -v python3 >/dev/null || return 0
	python3 - "$1" "$2" <<'PY' || true
import sys, wave, audioop
a, b = sys.argv[1], sys.argv[2]
wa, wb = wave.open(a, 'rb'), wave.open(b, 'rb')
print("  golden: %.2fs %dch  render: %.2fs %dch"
      % (wa.getnframes()/wa.getframerate(), wa.getnchannels(),
         wb.getnframes()/wb.getframerate(), wb.getnchannels()))
sw = wa.getsampwidth()
da, db = wa.readframes(wa.getnframes()), wb.readframes(wb.getnframes())
n = min(len(da), len(db))
if n:
    # audioop has no sub(); a - b == a + (-1 * b)
    diff = audioop.rms(audioop.add(da[:n], audioop.mul(db[:n], sw, -1.0), sw), sw)
    print("  rms(golden-render) over common span = %d" % diff)
# first differing byte -> approx sample time
fs = wa.getframerate()*wa.getnchannels()*sw
for i in range(n):
    if da[i] != db[i]:
        print("  first sample diff at ~%.3fs (byte %d)" % (i/fs, i)); break
else:
    print("  common span identical; lengths differ" if len(da)!=len(db) else "  identical")
PY
}

[ -x "$RENDER" ] || [ "$CMD" = reconstruct ] || { :; }  # render built below

case "$CMD" in
reconstruct)
	reconstruct_project
	;;
update)
	[ -d "$PROJ" ] || reconstruct_project
	build
	mkdir -p "$GOLDEN_DIR"
	wav="$(render "$GOLDEN_DIR/_update")"
	[ -n "$wav" ] || { echo "ERROR: render produced no WAV (see $GOLDEN_DIR/_update.log)"; exit 1; }
	cp "$wav" "$GOLDEN"
	sha256sum "$GOLDEN" | awk '{print $1}' >"$GOLDEN.sha256"
	rm -rf "$GOLDEN_DIR/_update" "$GOLDEN_DIR/_update.log"
	echo "golden ($MODE) updated: $GOLDEN"
	echo "  sha256 $(cat "$GOLDEN.sha256")"
	;;
check)
	[ -f "$GOLDEN" ] || { echo "no golden for $MODE yet — run: $0 update"; exit 2; }
	[ -d "$PROJ" ] || reconstruct_project
	build
	wav="$(render "$GOLDEN_DIR/_check")"
	[ -n "$wav" ] || { echo "FAIL — render produced no WAV (see $GOLDEN_DIR/_check.log)"; exit 1; }
	if cmp -s "$wav" "$GOLDEN"; then
		echo "PASS — $MODE matches golden ($(cut -c1-16 "$GOLDEN.sha256")…)"
		rm -rf "$GOLDEN_DIR/_check" "$GOLDEN_DIR/_check.log"
		exit 0
	fi
	echo "FAIL — $MODE differs from golden"
	echo "  golden sha256: $(sha256sum "$GOLDEN" | awk '{print $1}')"
	echo "  render sha256: $(sha256sum "$wav" | awk '{print $1}')"
	audio_diff "$GOLDEN" "$wav"
	echo "  render kept for inspection: $wav"
	exit 1
	;;
padsweep)
	# Layout-invariance guard: render the fixture at several DELUGE_SIM_HEAP_PAD values and assert every
	# output is byte-identical to the pad=0 render. This proves the render depends on NO allocation
	# address — no overread, no layout-coupled PRNG draw count, etc. (the bug class behind the
	# master-dither / shared-PRNG coupling). It is independent of the golden: it compares renders to each
	# other, so it catches layout fragility even when the golden happens to match a fixed layout.
	# Skip (exit 77 = CTest SKIP_RETURN_CODE) when the fixture song isn't available locally — the backup
	# songs aren't in CI, so a fixtureless runner skips rather than fails.
	if [ ! -d "$PROJ" ] && [ ! -f "$SONG_DIR/$SONG_XML" ]; then
		echo "SKIP — fixture '$FIXTURE' unavailable (no '$SONG_DIR/$SONG_XML'; set DELUGE_BACKUP)"; exit 77
	fi
	[ -d "$PROJ" ] || reconstruct_project
	build
	pads="${PADS:-16 64 256 4096}"
	ref="$GOLDEN_DIR/_padsweep_ref"
	PAD=0 wav0="$(render "$ref")"
	[ -n "$wav0" ] || { echo "FAIL — pad=0 render produced no WAV (see $ref.log)"; exit 1; }
	fails=0
	for p in $pads; do
		PAD="$p" wavp="$(render "$GOLDEN_DIR/_padsweep_$p")"
		if [ -z "$wavp" ]; then
			echo "FAIL — pad=$p render produced no WAV (see $GOLDEN_DIR/_padsweep_$p.log)"; fails=$((fails+1)); continue
		fi
		if cmp -s "$wav0" "$wavp"; then
			echo "PASS — $FIXTURE/$MODE pad=$p matches pad=0"
			rm -rf "$GOLDEN_DIR/_padsweep_$p" "$GOLDEN_DIR/_padsweep_$p.log"
		else
			echo "FAIL — $FIXTURE/$MODE pad=$p DIFFERS from pad=0 (layout-dependent render)"
			audio_diff "$wav0" "$wavp"
			echo "  kept: $wavp"; fails=$((fails+1))
		fi
	done
	rm -rf "$ref" "$ref.log"
	[ "$fails" -eq 0 ] || { echo "padsweep: $fails/$(echo $pads | wc -w) pad(s) diverged"; exit 1; }
	echo "padsweep: $FIXTURE/$MODE layout-invariant across pads [$pads]"
	;;
*)
	echo "usage: $0 [check|update|reconstruct|padsweep]   (MODE=$MODE)"; exit 2 ;;
esac
