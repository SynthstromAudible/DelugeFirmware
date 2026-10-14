"""The synth golden masters: every corpus preset, played and rendered as the device renders it, against its record.

Each case is one `render_preset` run (tests/integration/scenarios/render_preset_scenario.h). The card holds the preset
at SYNTHS/PRESET.XML, where the blank boot song picks it up, plus the case's samples. A DIN note script on MIDI channel 1
plays it through MIDI follow, which sends that channel to the instrument in front of the user, and the output is
captured from the audio tier. This module stages each case, runs them in parallel, and checks per case:
    the scenario's own lines and result, as every scenario gate does;
    RP-SILENT   the render is not silence. A preset that never heard its notes renders silence, and would otherwise
                record a hash like any other (a preset that failed to load is the scenario's RP-LOADED failure);
    RP-PCM      the output against its record in tests/integration/golden/presets.json: sha256, frames, peak and
                RMS level, the record render-song keeps.
And once per run:
    RP-LEARN    a preset's own MIDI learn reaches it: the blank boot song must keep the input a preset was saved with
                when it replaces its default synth. It is the one path the corpus cases do not play through.

The corpus is tests/integration/golden/presets/: `<case>.xml` (or `.XML`), optionally `<case>.notes` (default.notes
otherwise), `<case>.wav` and `<case>.2.wav` (staged as SAMPLES/WT.WAV and WT2.WAV, which the preset names). A case is
named by its path under the corpus without the extension, e.g. `factory/000 Rich Saw Bass`. The cases under `stereo/`
render with headphones plugged in, so in stereo (RENDER_STEREO); the rest render in mono, as through the speaker.

RENDER_PRESET_FILTER takes a comma-separated list of substrings, and runs the cases whose name holds any of them.
RENDER_PRESET_CORPUS=<dir> renders the cases of another copy of the corpus (e.g. its presets upgraded) against the same
records.
RENDER_PRESET_KEEP=<dir> keeps each case's output there, as `<case>.pcm` (interleaved little-endian i32, the case's
length exactly), for tests/integration/golden/compare_renders.py.
RENDER_RANDOM_SEED seeds the synth's random stream at each case's first note instead of its boot seed, to measure how
much a case depends on its random draws (audio/host.rs).
RENDER_PRESET_UPDATE=1 records the cases run instead of comparing them; a changed hash is a finding to explain in the
commit that records the new one.
"""

import concurrent.futures
import importlib.util
import json
import os
import re
import shutil
from pathlib import Path

from scenario_gate import int_field

FRAMES_PER_SECOND = 44100
DEFAULT_SECONDS = 2.0
# Per-case capture lengths, for the cases whose sound outlasts the default.
SECONDS = {"env_shape": 3.0}
# Cases whose sources read the audio input: fed the harness's test signal instead of silence.
INPUT = {"input_mono", "input_stereo", "input_pitchshift"}
# Cases under this directory render with headphones plugged in, so the app renders in stereo; every other case renders
# in mono, as through the device's speaker.
STEREO_DIR = "stereo/"
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
        for pattern in ("*.xml", "*.XML", "*.json")
        for p in corpus.rglob(pattern)
    )


def _preset(corpus: Path, name: str) -> Path:
    for suffix in (".xml", ".XML", ".json"):
        path = corpus / f"{name}{suffix}"
        if path.exists():
            return path
    raise FileNotFoundError(name)


# A preset's saved MIDI learn, in the attribute format, the old tag format and JSON.
LEARNED_INPUT = re.compile(
    r'\s*inputMidiChannel="[^"]*"|\s*<inputMidiChannel>[^<]*</inputMidiChannel>'
)
LEARNED_INPUT_JSON = re.compile(r'\s*"inputMidiChannel":\s*[^,}\n]*\s*,')
LEARNED_INPUT_JSON_LAST = re.compile(r',?\s*"inputMidiChannel":\s*[^,}\n]*(?=\s*\})')


def without_learned_input_json(text: str) -> str:
    """A JSON preset without its MIDI learn member, and without the comma that joined it to its neighbours."""
    return LEARNED_INPUT_JSON_LAST.sub("", LEARNED_INPUT_JSON.sub("", text))


def _staged_preset_text(preset: Path) -> str:
    """The preset as the card holds it: without any MIDI learn it was saved with.

    The note scripts reach the preset through MIDI follow. A learn on the same channel would deliver every note a second
    time, starting a second voice for it, so a case's sound would depend on whether the preset happens to be learned.
    """
    if preset.suffix == ".json":
        return without_learned_input_json(preset.read_text())
    return LEARNED_INPUT.sub("", preset.read_text())


def _stage(corpus: Path, name: str, where: Path) -> dict[str, str]:
    """Lay out the case's card and return the environment that renders it."""
    preset = _preset(corpus, name)
    card = where / "card"
    (card / "SYNTHS").mkdir(parents=True)
    staged = "PRESET.json" if preset.suffix == ".json" else "PRESET.XML"
    (card / "SYNTHS" / staged).write_text(_staged_preset_text(preset))
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
    if name.startswith(STEREO_DIR):
        env["RENDER_STEREO"] = "1"
    return env


def _wanted_frames(name: str) -> int:
    return round(SECONDS.get(name, DEFAULT_SECONDS) * FRAMES_PER_SECOND)


def _check_learn(gate, corpus: Path, pcm_record) -> None:
    """RP-LEARN: a preset learned to MIDI channel 2 must sound when played on channel 2, where MIDI follow does not
    listen, so only the preset's own learn can reach it."""
    where = gate.tmp / "learn"
    env = _stage(corpus, "sine_basic", where)
    preset = next((where / "card" / "SYNTHS").glob("PRESET.*"))
    learn = (
        ('"sound": {', '"sound": {"inputMidiChannel": 1,')
        if preset.suffix == ".json"
        else ("<sound", '<sound inputMidiChannel="1"')
    )
    preset.write_text(preset.read_text().replace(*learn, 1))
    notes = where / "channel2.notes"
    notes.write_text("4608 91 3C 64\n52000 81 3C 40\n")
    env["RENDER_MIDI"] = str(notes)
    result = gate.run(env=env, timeout=RUN_TIMEOUT)
    if not gate.require_standard(result):
        gate.fail("RP-LEARN: the run failed")
        return
    peak = pcm_record((where / "out.pcm").read_bytes())["peak"]
    gate.say(f"RP-LEARN peak={peak}")
    if peak < SILENCE_PEAK:
        gate.fail(
            "RP-LEARN — a preset learned to MIDI channel 2 is silent on channel 2: the blank boot song dropped the "
            "preset's MIDI learn"
        )


def run(gate):
    # RENDER_PRESET_CORPUS renders another copy of the corpus (e.g. its files upgraded) against the same records.
    corpus = Path(
        gate.env.get("RENDER_PRESET_CORPUS")
        or gate.root / "tests/integration/golden/presets"
    )
    records_path = gate.root / "tests/integration/golden/presets.json"
    update = os.environ.get("RENDER_PRESET_UPDATE") == "1"
    keep = (
        Path(gate.env["RENDER_PRESET_KEEP"])
        if gate.env.get("RENDER_PRESET_KEEP")
        else None
    )
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
        if keep is not None:
            kept = keep / f"{name}.pcm"
            kept.parent.mkdir(parents=True, exist_ok=True)
            kept.write_bytes(pcm[: wanted * 8])
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

    _check_learn(gate, corpus, pcm_record)

    if update:
        records_path.write_text(json.dumps(records, indent=1, sort_keys=True) + "\n")
        gate.say(
            f"RP-PCM recorded {len(selected) - failed} case(s) in {records_path.relative_to(gate.root)}"
        )
    gate.say(f"RP-CASES run={len(selected)} passed={len(selected) - failed}")
