"""The synth golden masters: every corpus preset, played and rendered as the device renders it, against its record.

Each case is one `render_preset` run (tests/integration/scenarios/render_preset_scenario.h). The card holds the preset
at SYNTHS/PRESET.XML, where the blank boot song picks it up, plus the case's samples. A DIN note script plays it, and
the output is captured from the audio tier. This module stages each case, runs them in parallel, and checks per case:
    the scenario's own lines and result, as every scenario gate does;
    RP-SILENT   the render is not silence. A preset that failed to load, or never heard its notes, renders silence,
                and would otherwise record a hash like any other;
    RP-PCM      the output against its record in tests/integration/golden/presets.json: sha256, frames, peak and
                RMS level, the record render-song keeps.

The corpus is tests/integration/golden/presets/: `<case>.xml` (or `.XML`), optionally `<case>.notes` (default.notes
otherwise), `<case>.wav` and `<case>.2.wav` (staged as SAMPLES/WT.WAV and WT2.WAV, which the preset names). A case is
named by its path under the corpus without the extension, e.g. `factory/000 Rich Saw Bass`.

RENDER_PRESET_FILTER takes a comma-separated list of substrings, and runs the cases whose name holds any of them.
RENDER_PRESET_UPDATE=1 records the cases run instead of comparing them; a changed hash is a finding to explain in the
commit that records the new one.
"""

import concurrent.futures
import importlib.util
import json
import os
import shutil
from pathlib import Path

from scenario_gate import int_field

FRAMES_PER_SECOND = 44100
DEFAULT_SECONDS = 2.0
# Per-case capture lengths, for the cases whose sound outlasts the default.
SECONDS = {"env_shape": 3.0}
# Cases whose sources read the audio input: fed the harness's test signal instead of silence.
INPUT = {"input_mono", "input_stereo", "input_pitchshift"}
# The loudest sample of silence: 16 in 16-bit terms, the C-host harness's threshold. The render's dither floor sits
# far below it.
SILENCE_PEAK = 16 << 16
RUN_TIMEOUT = 120  # seconds; a case takes well under one


def _render_song():
    """The render-song check module, for its PCM record."""
    spec = importlib.util.spec_from_file_location(
        "render_song", Path(__file__).with_name("render_song.py")
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def cases(corpus: Path) -> list[str]:
    """Every case in the corpus, by name."""
    return sorted(
        str(p.relative_to(corpus).with_suffix(""))
        for pattern in ("*.xml", "*.XML")
        for p in corpus.rglob(pattern)
    )


def _preset(corpus: Path, name: str) -> Path:
    for suffix in (".xml", ".XML"):
        path = corpus / f"{name}{suffix}"
        if path.exists():
            return path
    raise FileNotFoundError(name)


def _staged_preset_text(preset: Path) -> str:
    """The preset as the card holds it: bound to MIDI channel 1, which the note scripts play on.

    The synthesized cases carry inputMidiChannel="0" themselves. The factory and community presets are committed as
    published, so the binding goes in as a child tag, which the deserializer reads like an attribute in both the old
    tag-based format and the attribute-based one.
    """
    text = preset.read_text()
    if "inputMidiChannel" not in text:
        open_tag_end = text.index(">", text.index("<sound")) + 1
        text = (
            text[:open_tag_end]
            + "\n\t<inputMidiChannel>0</inputMidiChannel>"
            + text[open_tag_end:]
        )
    return text


def _stage(corpus: Path, name: str, where: Path) -> dict[str, str]:
    """Lay out the case's card and return the environment that renders it."""
    preset = _preset(corpus, name)
    card = where / "card"
    (card / "SYNTHS").mkdir(parents=True)
    (card / "SYNTHS" / "PRESET.XML").write_text(_staged_preset_text(preset))
    samples = [
        s
        for s in (preset.with_suffix(".wav"), preset.with_suffix(".2.wav"))
        if s.exists()
    ]
    if samples:
        (card / "SAMPLES").mkdir()
        for sample, dest in zip(samples, ("WT.WAV", "WT2.WAV")):
            shutil.copy(sample, card / "SAMPLES" / dest)
    notes = preset.with_suffix(".notes")
    if not notes.exists():
        notes = corpus / "default.notes"
    env = {
        "RENDER_TREE": str(card),
        "RENDER_MIDI": str(notes),
        "RENDER_SECONDS": str(SECONDS.get(name, DEFAULT_SECONDS)),
        "RENDER_PCM_OUT": str(where / "out.pcm"),
    }
    if name in INPUT:
        env["RENDER_INPUT"] = "1"
    return env


def _wanted_frames(name: str) -> int:
    return round(SECONDS.get(name, DEFAULT_SECONDS) * FRAMES_PER_SECOND)


def run(gate):
    corpus = gate.root / "tests/integration/golden/presets"
    records_path = gate.root / "tests/integration/golden/presets.json"
    update = os.environ.get("RENDER_PRESET_UPDATE") == "1"
    pcm_record = _render_song()._pcm_record

    selected = cases(corpus)
    needles = [n for n in gate.env.get("RENDER_PRESET_FILTER", "").split(",") if n]
    if needles:
        selected = [c for c in selected if any(n in c for n in needles)]
    if not selected:
        gate.fail("no case selected")
        return

    def render(index_name):
        index, name = index_name
        where = gate.tmp / f"case{index:03d}"
        env = _stage(corpus, name, where)
        return name, where, gate.run(env=env, timeout=RUN_TIMEOUT)

    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 1) as pool:
        runs = list(pool.map(render, enumerate(selected)))

    records = json.loads(records_path.read_text()) if records_path.exists() else {}
    failed = 0
    for name, where, result in runs:
        before = len(gate.failures)
        if not gate.require_standard(result):
            gate.fail(f"RP-CASE {name}: the run failed")
            failed += 1
            continue
        frames = int_field(result.last("RP-CAPTURE "), "frames") or 0
        wanted = _wanted_frames(name)
        pcm = (where / "out.pcm").read_bytes()
        if frames < wanted or len(pcm) < wanted * 8:
            gate.fail(
                f"RP-CAPTURE {name}: captured {frames} frames, fewer than {wanted}"
            )
            failed += 1
            continue
        # Whole blocks are captured; the record covers exactly the case's length.
        record = pcm_record(pcm[: wanted * 8])
        if record["peak"] < SILENCE_PEAK:
            gate.fail(f"RP-SILENT {name}: the render is silent (peak {record['peak']})")
        elif update:
            records[name] = record
        elif name not in records:
            gate.fail(
                f"RP-PCM {name}: no record; run with RENDER_PRESET_UPDATE=1 to make one"
            )
        elif records[name] != record:
            gate.fail(
                f"RP-PCM {name}: the rendered output changed: recorded {records[name]}, rendered {record}"
            )
        if len(gate.failures) != before:
            failed += 1

    if update:
        records_path.write_text(json.dumps(records, indent=1, sort_keys=True) + "\n")
        gate.say(
            f"RP-PCM recorded {len(selected) - failed} case(s) in {records_path.relative_to(gate.root)}"
        )
    gate.say(f"RP-CASES run={len(selected)} passed={len(selected) - failed}")
