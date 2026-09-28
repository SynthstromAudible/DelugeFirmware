"""The offline stem export's regression gate.

Renders the fixture's MIXDOWN (cordae unless GOLDEN_FIXTURE names another) through the Embassy host BSP's own offline
export -- the default, scenario-less arm of golden_vt_render -- and checks:

  ST-BYTES    the export actually wrote audio, at the size recorded beside the hash (field 2 of the baseline). Checked
              before the hash so a zero-byte export reports what actually happened rather than an unhelpful mismatch.
  ST-RENDER   the exported WAV's sha256 matches harness/stem-export-<fixture>.sha256 (field 1).

THIS IS NOT golden-embassy-diff, AND ITS BASELINE IS NOT THAT GATE'S GOLDEN. That gate compares against a golden kept
outside the repo in DELUGE_GOLDEN_DIR, reconstructed from the song corpus. This one compares the Embassy render against
ITSELF, recorded in the repo, so it needs no corpus.

THE BASELINE IS NOT EXPECTED TO MOVE. A walk's cadence is an implementation detail; rendered audio is not. If
ST-RENDER goes red, that is a finding to investigate -- never a re-baseline.
"""

import hashlib
import re


def run(gate):
    fixture = gate.env.get("GOLDEN_FIXTURE") or "cordae"
    baseline = (gate.root / f"harness/stem-export-{fixture}.sha256").read_text().split()
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
        for line in [
            line for line in log if re.match(r"ST-|\[golden-scenario\]", line)
        ][-20:]:
            gate.say(line)
        return

    for line in log:
        if line.startswith("ST-"):
            gate.say(line)

    sizes = [
        m.group(1)
        for m in (re.match(r"ST-BYTES size=([0-9]*) ", line) for line in log)
        if m
    ]
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

    if not any(line.startswith("ST-BYTES ") for line in log):
        gate.fail("ST-BYTES never appeared; the gate did not observe what it claims to")

    if gate.failures:
        gate.say(f"ST-RESULT failures={len(gate.failures)}")
        return
    gate.say("ST-RESULT failures=0")
    for line in log:
        if re.match(r"TIER-SYNC(-AT)? ", line):
            gate.say(line)
    gate.say(
        f"PASS — the export rendered byte-identical audio, and wrote {written} bytes"
    )
