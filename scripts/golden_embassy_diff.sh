#!/usr/bin/env bash
#
# Embassy-vs-golden differential: renders `golden_vt_render` (the Embassy-host-BSP
# golden renderer, `src/bsp/rust/harness/golden_vt_render/`) in MIXDOWN mode for one fixture
# and byte-compares its output against the SAME established golden baseline
# `scripts/golden_mixdown.sh` gates the C-host `deluge_render` sim against — not a
# fresh reference, the actual `<fixture>_MIXDOWN.golden.wav.sha256` in
# `DELUGE_GOLDEN_DIR`. This is the reusable convergence gate for the
# golden-on-Embassy work: run it after any change that could perturb the Embassy
# renderer's output (sim_latency/config changes, streaming-fill relocations, etc.)
# to prove the Rust/Embassy BSP still produces bit-exact golden output.
#
# Usage:
#   scripts/golden_embassy_diff.sh <fixture> [check|update|padsweep]   # cordae | highsiderr | icoustic
#     check  (default) — render and byte-compare against the stored golden baseline
#     update           — render and RECORD the output as the new golden baseline
#                        (<fixture>_MIXDOWN.golden.wav + .sha256 in DELUGE_GOLDEN_DIR).
#                        This is the async re-baseline tool: golden_mixdown.sh's own
#                        `update` renders the SYNC C-host deluge_render, so it is NOT
#                        how the async Embassy baseline gets captured.
#     padsweep         — render at several DELUGE_SIM_HEAP_PAD values (PADS env, default
#                        "16 64 256 4096") and assert every output is byte-identical to
#                        the pad=0 render. The layout-invariance guard: proves the render
#                        depends on NO allocation address. Golden-INDEPENDENT (compares
#                        renders to each other), so it catches layout fragility even when
#                        the golden matches one fixed layout. Reimplements golden_mixdown.sh's
#                        retired C-host `padsweep` on the Embassy renderer.
#
# Exit: 0 = PASS (byte-identical to the stored golden) / update wrote the baseline,
# 1 = mismatch or a render crash/wedge, 2 = setup error (no baseline yet, no corpus, etc.)
#
# Env overrides (same conventions as golden_mixdown.sh / sd_image.rs):
#   DELUGE_GOLDEN_DIR         where the reconstructed project + golden baseline live
#                             (default: ~/.cache/deluge-golden)
#   DELUGE_BACKUP             the "Deluge Backup" root, used to reconstruct a fixture's
#                             project tree the first time (default: ~/Deluge Backup)
#   BUILD_DIR                 the build-embassy-hostapp C++ object-closure build dir
#                             (default: <repo>/build-embassy-hostapp)
#   GOLDEN_VIRTUAL_BUDGET_MS  virtual-time wedge-detection budget passed through to
#                             golden_vt_render (default: 1800000 = 30 simulated minutes;
#                             cheap to over-provision — the discrete-event driver jumps
#                             straight to the next due deadline, so a large budget costs
#                             no extra wall time unless the render actually needs it)
#   NO_BUILD=1                skip the ninja/cargo rebuild steps
#   PADS                      pad sizes for `padsweep`          (default: "16 64 256 4096")
#   PAD                       one-off DELUGE_SIM_HEAP_PAD for check/update  (default: 0)
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO/build-embassy-hostapp}"
GOLDEN_DIR="${DELUGE_GOLDEN_DIR:-$HOME/.cache/deluge-golden}"
CRATE_DIR="$REPO/src/bsp/rust/harness/golden_vt_render"

FIXTURE="${1:-}"
case "$FIXTURE" in
cordae | highsiderr | icoustic) ;;
*)
	echo "usage: $0 <cordae|highsiderr|icoustic> [check|update|padsweep]"
	exit 2
	;;
esac

VERB="${2:-check}"
case "$VERB" in
check | update | padsweep) ;;
*)
	echo "usage: $0 <cordae|highsiderr|icoustic> [check|update|padsweep]"
	exit 2
	;;
esac

GOLDEN_WAV_FILE="$GOLDEN_DIR/${FIXTURE}_MIXDOWN.golden.wav"
GOLDEN_SHA_FILE="$GOLDEN_WAV_FILE.sha256"
if [ "$VERB" = check ]; then
	[ -f "$GOLDEN_SHA_FILE" ] || {
		echo "ERROR: no MIXDOWN golden baseline for '$FIXTURE' yet — run:"
		echo "  $0 $FIXTURE update"
		exit 2
	}
	GOLDEN_SHA="$(cut -c1-64 "$GOLDEN_SHA_FILE")"
fi

if [ "${NO_BUILD:-0}" != 1 ]; then
	[ -d "$BUILD_DIR" ] || {
		echo "ERROR: '$BUILD_DIR' does not exist — configure build-embassy-hostapp from sim/ first"
		exit 2
	}
	# Cargo does not track the C++ sources golden_vt_render links against — force a
	# rebuild of the host object closure exactly like the plan's build reference does.
	ninja -C "$BUILD_DIR" deluge_app NE10 eyalroz_printf deluge_dsp deluge_scheduler \
		deluge_foundation deluge_midi
	(cd "$CRATE_DIR" && cargo build --release)
fi

# Honour CARGO_TARGET_DIR: `dbt harness run` points every rig at the shared target/harness/ tree,
# so the crate-local target/ is empty there and a hard-coded path fails after a SUCCESSFUL build.
BIN="${CARGO_TARGET_DIR:-$CRATE_DIR/target}/release/golden_vt_render"
[ -x "$BIN" ] || { echo "ERROR: '$BIN' not built (see NO_BUILD)"; exit 2; }

OUT_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/golden_embassy_diff.${FIXTURE}.XXXXXX")"
cleanup() { rm -rf "$OUT_ROOT"; }
trap cleanup EXIT

# Render the fixture once at a given DELUGE_SIM_HEAP_PAD (0 = no pad — the C++ closure is built
# -DDELUGE_DETERMINISTIC_ALLOC, which leaks that many SDRAM bytes before the slab to shift layout).
# Echoes the produced WAV path on success; prints a diagnostic to stderr and echoes nothing on failure.
# $1 = pad bytes, $2 = tag (out subdir + log name).
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

if [ "$VERB" = padsweep ]; then
	pads="${PADS:-16 64 256 4096}"
	wav0="$(render_pad 0 pad0)"
	[ -n "$wav0" ] || { echo "FAIL — $FIXTURE pad=0 render produced no WAV"; exit 1; }
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
		echo "padsweep: $FIXTURE — $fails/$(echo $pads | wc -w) pad(s) diverged"
		trap - EXIT # keep renders for inspection
		echo "  renders kept: $OUT_ROOT"
		exit 1
	fi
	echo "padsweep: $FIXTURE MIXDOWN layout-invariant across pads [$pads]"
	exit 0
fi

# check / update: a single render at pad=0 (or a one-off PAD override).
wav="$(render_pad "${PAD:-0}" render)"
if [ -z "$wav" ]; then
	echo "FAIL — golden_vt_render produced no usable MIXDOWN WAV for fixture=$FIXTURE (see the line above)"
	trap - EXIT
	exit 1
fi
render_sha="$(sha256sum "$wav" | awk '{print $1}')"

if [ "$VERB" = update ]; then
	mkdir -p "$GOLDEN_DIR"
	cp "$wav" "$GOLDEN_WAV_FILE"
	printf '%s\n' "$render_sha" >"$GOLDEN_SHA_FILE"
	echo "UPDATED — $FIXTURE MIXDOWN async golden recorded (${render_sha:0:16}…)"
	echo "  wav:    $GOLDEN_WAV_FILE"
	echo "  sha256: $GOLDEN_SHA_FILE"
	exit 0
fi

if [ "$render_sha" = "$GOLDEN_SHA" ]; then
	echo "PASS — $FIXTURE MIXDOWN matches golden (${GOLDEN_SHA:0:16}…)"
	exit 0
fi

echo "FAIL — $FIXTURE MIXDOWN differs from golden"
echo "  golden sha256: $GOLDEN_SHA"
echo "  render sha256: $render_sha"
echo "  render kept for inspection: $wav"
trap - EXIT
exit 1
