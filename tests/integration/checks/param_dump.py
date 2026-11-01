"""The parameter dump seam (src/deluge/processing/sound/param_dump.h): a dumped render sounds the same as a plain one and
holds every pass.

    PD-SAME     the render's output is byte-identical with dumping on and off
    PD-PARSE    the dump reads back, version 1
    PD-PASSES   a sound-scope pass, and a voice-scope pass and source states for each note-on
    PD-ORDER    each key's times never go back
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(
    0, str(Path(__file__).resolve().parents[3] / "tools" / "audible_window")
)

import render_preset
from dump import records

CASE = "voice/depth_velocity_poly"
NOTE_ONS = 2  # the case's .notes script


def _render(gate, corpus: Path, where: Path, dump: Path | None) -> bytes | None:
    where.mkdir()
    env = render_preset._stage(corpus, CASE, where)
    if dump is not None:
        env["RENDER_PARAM_DUMP"] = str(dump)
    run = gate.run(env=env, timeout=render_preset.RUN_TIMEOUT)
    if run.code != 0:
        gate.fail(f"the {'dumped' if dump else 'plain'} render exited {run.code}")
        return None
    return (where / "out.pcm").read_bytes()


def run(gate):
    corpus = gate.root / "tests" / "integration" / "golden" / "presets"
    dump_path = gate.tmp / "dump.params"
    plain = _render(gate, corpus, gate.tmp / "plain", None)
    dumped = _render(gate, corpus, gate.tmp / "dumped", dump_path)
    if plain is None or dumped is None:
        return
    if plain != dumped:
        gate.fail("PD-SAME: dumping changed the render")
    else:
        gate.say("PD-SAME ok")
    try:
        local_params, global_params, recs = records(dump_path)
    except ValueError as exc:
        gate.fail(f"PD-PARSE: {exc}")
        return
    gate.say(
        f"PD-PARSE ok local={local_params} global={global_params} records={len(recs)}"
    )
    sound_passes = [r for r in recs if r.kind == 0 and r.scope == 1]
    voices = {r.voice for r in recs if r.kind == 0 and r.scope == 0}
    with_sources = {r.voice for r in recs if r.kind == 1}
    if not sound_passes or len(voices) < NOTE_ONS or len(with_sources) < NOTE_ONS:
        gate.fail(
            f"PD-PASSES: sound passes {len(sound_passes)}, voices {len(voices)}, voices with source states "
            f"{len(with_sources)} (want 1+, {NOTE_ONS}, {NOTE_ONS})"
        )
    else:
        gate.say(f"PD-PASSES ok voices={sorted(voices)}")
    last: dict = {}
    for r in recs:
        key = (r.kind, r.sound, r.scope, r.voice)
        if r.time < last.get(key, 0):
            gate.fail(f"PD-ORDER: {key} goes back in time at {r.time}")
            break
        last[key] = r.time
