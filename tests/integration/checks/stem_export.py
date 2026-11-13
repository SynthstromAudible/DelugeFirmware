"""The offline stem export's regression gate.

Renders the fixture's MIXDOWN (cordae unless GOLDEN_FIXTURE names another) through the Embassy host BSP's own offline
export -- run through the stem_export row of golden_vt_render's table -- and checks:

  ST-BYTES    the export actually wrote audio, at the size recorded beside the hash (field 2 of the baseline). Checked
              before the hash so a zero-byte export reports what actually happened rather than an unhelpful mismatch.
  ST-RENDER   the exported WAV's sha256 matches stem-export-<fixture>.sha256, beside this module (field 1).

The scenario prints the ST-RESULT line itself; the gate judges it with the entry's markers.

The baseline is the Embassy render itself, recorded in the repo.

THE BASELINE IS NOT EXPECTED TO MOVE. A walk's cadence is an implementation detail; rendered audio is not. If
ST-RENDER goes red, that is a finding to investigate -- never a re-baseline.
"""

import hashlib
from pathlib import Path

from scenario_gate import field, starting


def run(gate):
    fixture = gate.env.get("GOLDEN_FIXTURE") or "cordae"
    baseline = (
        Path(__file__).with_name(f"stem-export-{fixture}.sha256").read_text().split()
    )
    want_hash, want_bytes = baseline[0], baseline[1]
    render = gate.tmp / "render"

    result = gate.run(
        env={
            "GOLDEN_FIXTURE": fixture,
            "GOLDEN_STEM_MODE": "MIXDOWN",
            "GOLDEN_STEM_OUT": str(render),
            "GOLDEN_VIRTUAL_BUDGET_MS": gate.env.get("GOLDEN_VIRTUAL_BUDGET_MS")
            or "1800000",
        }
    )
    # ST-BYTES is printed on stderr, among the export's own progress lines.
    log = result.lines + result.err.splitlines()
    if result.code != 0:
        gate.fail(f"the render exited {result.code} (crash or wedge)")
        for line in starting(log, "ST-", "[golden-scenario]")[-20:]:
            gate.say(line)
        return

    for line in starting(log, "ST-"):
        gate.say(line)

    # ST-BYTES is on stderr, the result line on stdout: the entry's markers and result are judged over both.
    if not gate.require_markers(log):
        return

    sizes = [field(line, "size") or "" for line in starting(log, "ST-BYTES ")]
    written = sizes[-1] if sizes else ""
    if written in ("", "0"):
        gate.fail(
            f"ST-BYTES — the export wrote {written or 'no'} bytes: it reported success and produced nothing"
        )
    elif written != want_bytes:
        gate.fail(f"ST-BYTES — the export wrote {written} bytes, recorded {want_bytes}")

    wavs = (
        sorted(path for path in render.rglob("*.WAV") if path.is_file())
        if render.is_dir()
        else []
    )
    if not wavs:
        gate.fail("ST-RENDER — the export produced no WAV to hash")
    else:
        got = hashlib.sha256(wavs[0].read_bytes()).hexdigest()
        gate.say(f"ST-RENDER sha256={got}")
        if got != want_hash:
            gate.fail("ST-RENDER — the rendered audio changed")
            gate.say(f"  recorded: {want_hash}")
            gate.say(f"  rendered: {got}")
            gate.say("  This is a FINDING, not a re-baseline. Bisect it.")

    if gate.failures:
        return
    for line in starting(log, "TIER-SYNC ", "TIER-SYNC-AT "):
        gate.say(line)
    gate.say(
        f"PASS — the export rendered byte-identical audio, and wrote {written} bytes"
    )
