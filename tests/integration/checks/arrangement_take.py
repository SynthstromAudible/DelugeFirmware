"""The arrangement-take gate: a session recorded into the arrangement, its takes repeating their clips' automation.

`render_song` plays the generated song RS_ARRANGE_TAKE (a synth, a kit and an audio clip, each one bar long and
automated) recording the session into the arrangement from the start (RENDER_ARRANGE_RECORD=1), and turns a knob on
each clip while it does (RENDER_KNOB), which clones the clip into a take. Each take repeats its source clip's notes and
automation at every loop end. The scenario prints:
    RS-TAKE         each take's length and lanes, from the model after the run. Byte-compared with the baseline: the
                    lanes each repeat appended, and those the knobs recorded, as the app made them when the baseline
                    was recorded.
    RS-TAKE-PLAYED  at each loop end of a take (a row's own end included), on the render: what it plays across the
                    repeat (its working copy of a lane, else the lane's published snapshot) against the source's lanes
                    shifted into the repeat, at each of their nodes. Every one must match: a repeat whose lanes are not
                    what the render plays plays the old lanes.
    RS-KNOB         every knob turn landed in its clip's take.
    RS-PCM          the rendered output, against the baseline's.
The scenario itself fails a run in which the render read or wrote a lane in the model (RS-SNAPSHOT) or made a param
collection, a repeat was not staged and swapped in at its loop end or an audio take found no spare (RS-TAKE-REPEATS),
a spare or a staged snapshot was left once recording ended (RS-TAKE-LEFT), or a collection's published lanes are not
its model's after the take (RS-TAKE-STALE).

ARRANGE_TAKE_UPDATE=1 records the baseline (tests/golden/arrangement_take_baselines/RS_ARRANGE_TAKE.txt) instead of
comparing with it, whatever else the run reports: a baseline is recorded on the commit the gate compares against.
"""

import importlib.util
import os
from pathlib import Path

from scenario_gate import int_field


def _load_sibling(name: str):
    spec = importlib.util.spec_from_file_location(
        name, Path(__file__).with_name(f"{name}.py")
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


render_song = _load_sibling("render_song")

SONG = "RS_ARRANGE_TAKE"
BASELINE = Path("tests/golden/arrangement_take_baselines") / f"{SONG}.txt"


def _pcm_line(pcm: bytes) -> str:
    record = render_song._pcm_record(pcm)
    return f"RS-PCM sha256={record['sha256']} frames={record['frames']} peak={record['peak']} rms_db={record['rms_db']}"


def run(gate):
    env = render_song._generate(gate, SONG)
    if env is None:
        return
    pcm_path = gate.tmp / "out.pcm"
    env["RENDER_PCM_OUT"] = str(pcm_path)
    result = gate.run(env=env)
    takes = result.starting("RS-TAKE ")
    pcm = pcm_path.read_bytes() if pcm_path.exists() else b""
    observed = takes + [_pcm_line(pcm)]
    baseline_path = gate.root / BASELINE
    if os.environ.get("ARRANGE_TAKE_UPDATE") == "1":
        baseline_path.parent.mkdir(parents=True, exist_ok=True)
        baseline_path.write_text("\n".join(observed) + "\n")
        gate.say(f"RS-TAKE recorded {len(takes)} lines in {BASELINE}")
        gate.show(result)
        return
    if not gate.require_standard(result):
        return
    gate.show(result)
    for line in result.starting("RS-TAKE-PLAYED clip="):
        if int_field(line, "mismatched") != 0:
            gate.fail(
                f"RS-TAKE-PLAYED — the render plays other lanes than the take's: {line}"
            )
    if not baseline_path.exists():
        gate.fail(
            f"RS-TAKE — no baseline at {BASELINE}; record one with ARRANGE_TAKE_UPDATE=1"
        )
        return
    expected = baseline_path.read_text().splitlines()
    if observed[:-1] != expected[:-1]:
        for want, got in zip(expected, observed):
            if want != got:
                gate.fail(f"RS-TAKE — first difference: baseline `{want}`, run `{got}`")
                break
        else:
            gate.fail(
                f"RS-TAKE — {len(observed) - 1} lines, the baseline {len(expected) - 1}"
            )
    if observed[-1] != expected[-1]:
        gate.fail(
            f"RS-PCM — the rendered output changed: baseline `{expected[-1]}`, run `{observed[-1]}`"
        )
