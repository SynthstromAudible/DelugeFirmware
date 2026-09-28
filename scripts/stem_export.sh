#!/usr/bin/env bash
# stem-export — the regression gate for the offline stem export.
#
# Renders the cordae MIXDOWN through the Embassy host BSP's own offline export (the DEFAULT
# GOLDEN_SCENARIO arm of golden_vt_render) and checks three things:
#
#   ST-RENDER   the exported WAV's sha256 matches harness/stem-export-cordae.sha256 (field 1)
#   ST-BYTES    the export actually wrote audio, at the size recorded beside the hash (field 2)
#
# ⚠️ THIS IS NOT golden-embassy-diff, AND ITS BASELINE IS NOT THAT GATE'S GOLDEN. That gate
# compares against a golden kept outside the repo in DELUGE_GOLDEN_DIR, reconstructed from the song
# corpus. This one compares the Embassy render against ITSELF, recorded here, so it needs no corpus.
#
# ⚠️ THE BASELINE IS NOT EXPECTED TO MOVE. A walk's cadence is an implementation detail; rendered
# audio is not. If ST-RENDER goes red, that is a finding to investigate — never a re-baseline.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

FIXTURE="${GOLDEN_FIXTURE:-cordae}"
BASELINE="harness/stem-export-$FIXTURE.sha256"
CRATE_DIR="src/bsp/rust/harness/golden_vt_render"
# Where the harness binary is: `dbt test` builds it once, before any gate runs, into CARGO_TARGET_DIR. Every
# scenario gate runs the same binary; GOLDEN_SCENARIO picks the scenario at run time.
TARGET_DIR="${TARGET_DIR:-${CARGO_TARGET_DIR:-$CRATE_DIR/target}}"
case "$TARGET_DIR" in /*) ;; *) TARGET_DIR="$REPO_ROOT/$TARGET_DIR" ;; esac

BIN="$TARGET_DIR/release/golden_vt_render"
[ -x "$BIN" ] || { echo "ERROR: '$BIN' is not built -- run this gate through ./dbt test, which builds it first"; exit 2; }

OUT_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/stem_export.$FIXTURE.XXXXXX")"
cleanup() { rm -rf "$OUT_ROOT"; }
trap cleanup EXIT

# The card image is cached and SHARED between gates, and reused-and-mutated across runs. Start cold.
rm -f "${TMPDIR:-/tmp}"/deluge-streaming-scenario-*.img

set +e
GOLDEN_FIXTURE="$FIXTURE" \
	GOLDEN_STEM_MODE=MIXDOWN \
	GOLDEN_STEM_OUT="$OUT_ROOT/render" \
	GOLDEN_VIRTUAL_BUDGET_MS="${GOLDEN_VIRTUAL_BUDGET_MS:-1800000}" \
	"$BIN" >"$OUT_ROOT/run.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
	echo "FAIL — the render exited $status (crash or wedge)"
	grep -E '^(ST-|\[golden-scenario\])' "$OUT_ROOT/run.log" | tail -20
	exit 1
fi

grep -E '^ST-' "$OUT_ROOT/run.log" || true

failures=0

# ST-BYTES: the export wrote audio at all. Checked before the hash so a zero-byte export reports
# what actually happened rather than an unhelpful mismatch.
bytes="$(sed -n 's/^ST-BYTES size=\([0-9]*\) .*/\1/p' "$OUT_ROOT/run.log" | tail -1)"
want_bytes="$(awk 'NR==1 {print $2}' "$BASELINE")"
if [ -z "$bytes" ] || [ "$bytes" = "0" ]; then
	echo "FAIL ST-BYTES — the export wrote ${bytes:-no} bytes: it reported success and produced nothing"
	failures=$((failures + 1))
elif [ "$bytes" != "$want_bytes" ]; then
	echo "FAIL ST-BYTES — the export wrote $bytes bytes, recorded $want_bytes"
	failures=$((failures + 1))
fi

# ST-RENDER: the audio is byte-identical to what this branch recorded.
WAV="$(find "$OUT_ROOT/render" -name '*.WAV' -type f | head -1)"
if [ -z "$WAV" ]; then
	echo "FAIL ST-RENDER — the export produced no WAV to hash"
	failures=$((failures + 1))
else
	got="$(sha256sum "$WAV" | cut -d' ' -f1)"
	want="$(awk 'NR==1 {print $1}' "$BASELINE")"
	echo "ST-RENDER sha256=$got"
	if [ "$got" != "$want" ]; then
		echo "FAIL ST-RENDER — the rendered audio changed"
		echo "  recorded: $want"
		echo "  rendered: $got"
		echo "  This is a FINDING, not a re-baseline. Bisect it."
		failures=$((failures + 1))
	fi
fi


for marker in ST-BYTES ST-RENDER; do
	if ! grep -q "^$marker " "$OUT_ROOT/run.log" && [ "$marker" != "ST-RENDER" ]; then
		echo "FAIL — $marker never appeared; the gate did not observe what it claims to"
		failures=$((failures + 1))
	fi
done

if [ "$failures" -ne 0 ]; then
	echo "ST-RESULT failures=$failures"
	exit 1
fi
echo "ST-RESULT failures=0"
grep -hE '^TIER-SYNC(-AT)? ' "$OUT_ROOT/run.log" || true
echo "PASS — the export rendered byte-identical audio, and wrote $bytes bytes"
