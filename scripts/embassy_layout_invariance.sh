#!/usr/bin/env bash
#
# Layout invariance of the Embassy host render: renders `golden_vt_render`
# (`src/bsp/rust/harness/golden_vt_render/`) in MIXDOWN mode for one fixture at several
# DELUGE_SIM_HEAP_PAD values and asserts every output is byte-identical to the pad=0 render.
# The C++ closure is built -DDELUGE_DETERMINISTIC_ALLOC, which leaks DELUGE_SIM_HEAP_PAD bytes of
# SDRAM before the slab, so each pad shifts every allocation address. Identical renders prove the
# audio depends on no allocation address. The renders are compared with each other, never with a
# stored baseline, so this catches layout fragility that a single recorded hash cannot.
#
# Usage:
#   scripts/embassy_layout_invariance.sh <fixture>   # cordae | highsiderr | icoustic
#
# Exit: 0 = every pad matches pad=0, 1 = a pad diverged or a render crashed/wedged, 2 = setup error.
#
# Env overrides:
#   DELUGE_GOLDEN_DIR         where the fixture's reconstructed project tree is cached
#                             (default: ~/.cache/deluge-golden)
#   DELUGE_BACKUP             the "Deluge Backup" root that tree is reconstructed from
#                             (default: ~/Deluge Backup)
#   GOLDEN_VIRTUAL_BUDGET_MS  virtual-time wedge-detection budget passed through to golden_vt_render
#                             (default: 1800000 = 30 simulated minutes; the discrete-event driver jumps
#                             straight to the next due deadline, so a large budget costs no wall time
#                             unless the render needs it)
#   PADS                      pad sizes compared against pad=0 (default: "16 64 256 4096")
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CRATE_DIR="$REPO/src/bsp/rust/harness/golden_vt_render"

FIXTURE="${1:-}"
case "$FIXTURE" in
cordae | highsiderr | icoustic) ;;
*)
	echo "usage: $0 <cordae|highsiderr|icoustic>"
	exit 2
	;;
esac

# `dbt test` builds the harness once, before any gate runs, into CARGO_TARGET_DIR.
BIN="${CARGO_TARGET_DIR:-$CRATE_DIR/target}/release/golden_vt_render"
[ -x "$BIN" ] || { echo "ERROR: '$BIN' is not built -- run this gate through ./dbt test, which builds it first"; exit 2; }

OUT_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/embassy_layout_invariance.${FIXTURE}.XXXXXX")"
cleanup() { rm -rf "$OUT_ROOT"; }
trap cleanup EXIT

# Render the fixture once at a given DELUGE_SIM_HEAP_PAD. Echoes the produced WAV path on success;
# prints a diagnostic to stderr and echoes nothing on failure. $1 = pad bytes, $2 = tag (out subdir + log name).
render_pad() {
	local pad="$1" tag="$2"
	local out="$OUT_ROOT/$tag"
	mkdir -p "$out"
	set +e
	GOLDEN_FIXTURE="$FIXTURE" \
		GOLDEN_STEM_MODE=MIXDOWN \
		GOLDEN_STEM_OUT="$out" \
		GOLDEN_VIRTUAL_BUDGET_MS="${GOLDEN_VIRTUAL_BUDGET_MS:-1800000}" \
		DELUGE_SIM_HEAP_PAD="$pad" \
		"$BIN" >"$out.log" 2>&1
	local status=$?
	set -e
	if [ "$status" -ne 0 ]; then
		echo "  render (pad=$pad) exited $status (crash or wedge) — log: $out.log" >&2
		return
	fi
	local w
	w="$(find "$out" -iname '*.wav' | sort | head -1)"
	if [ -z "$w" ]; then
		echo "  render (pad=$pad) produced no WAV — log: $out.log" >&2
		return
	fi
	# A MIXDOWN export writes exactly one WAV. Anything else came from a folder this render did not write -- an
	# earlier export left on the shared card image -- and hashing it would measure that file, not this render.
	local n
	n="$(find "$out" -iname '*.wav' | wc -l)"
	if [ "$n" -ne 1 ] || ! basename "$w" | grep -qi '^MIXDOWN_'; then
		echo "  render (pad=$pad) copied $n WAV(s), first $(basename "$w"), not this render's one MIXDOWN — the stems were read from the wrong folder — log: $out.log" >&2
		return
	fi
	echo "$w"
}

pads="${PADS:-16 64 256 4096}"
wav0="$(render_pad 0 pad0)"
[ -n "$wav0" ] || { echo "FAIL — $FIXTURE pad=0 render produced no WAV"; trap - EXIT; echo "  renders kept: $OUT_ROOT"; exit 1; }
echo "$FIXTURE MIXDOWN pad=0 sha256 $(sha256sum "$wav0" | awk '{print $1}')"
fails=0
for p in $pads; do
	wavp="$(render_pad "$p" "pad$p")"
	if [ -z "$wavp" ]; then
		echo "FAIL — $FIXTURE pad=$p render failed"
		fails=$((fails + 1))
		continue
	fi
	if cmp -s "$wav0" "$wavp"; then
		echo "PASS — $FIXTURE MIXDOWN pad=$p matches pad=0"
	else
		echo "FAIL — $FIXTURE MIXDOWN pad=$p DIFFERS from pad=0 (layout-dependent render)"
		echo "  pad0:  $(sha256sum "$wav0" | awk '{print $1}')"
		echo "  pad$p: $(sha256sum "$wavp" | awk '{print $1}')"
		fails=$((fails + 1))
	fi
done
if [ "$fails" -ne 0 ]; then
	echo "layout invariance: $FIXTURE — $fails/$(echo $pads | wc -w) pad(s) diverged"
	trap - EXIT # keep renders for inspection
	echo "  renders kept: $OUT_ROOT"
	exit 1
fi
echo "layout invariance: $FIXTURE MIXDOWN identical across pads [0 $pads]"
