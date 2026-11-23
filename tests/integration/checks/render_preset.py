"""The synth golden masters: every corpus preset, played and rendered as the device renders it, against its record.

Each case is one `render_preset` run (tests/integration/scenarios/render_preset_scenario.cpp). The card holds the preset
at SYNTHS/PRESET.XML, where the blank boot song picks it up, plus the case's samples. A DIN note script on MIDI channel 1
plays it through MIDI follow, which sends that channel to the instrument in front of the user, and the output is
captured from the audio tier. This module stages each case, runs them in parallel, and checks per case:
    the scenario's own lines and result, as every scenario gate does;
    RP-SILENT   the render is not silence. A preset that never heard its notes renders silence, and would otherwise
                record a hash like any other (a preset that failed to load is the scenario's RP-LOADED failure);
    RP-PCM      the output against its record in tests/golden/render/presets.json: sha256, frames, peak and
                RMS level, the record render-song keeps.
    RP-ZONE     a case in ZONE_END moved its sample zone's end mid-note (RENDER_ZONE_END, as the sample marker editor
                moves it).
    RP-SETTING  a case in SETTING changed a setting mid-render (RENDER_SETTING, as its menu changes it).
And once per run:
    RP-LEARN    a preset's own MIDI learn reaches it: the blank boot song must keep the input a preset was saved with
                when it replaces its default synth. It is the one path the corpus cases do not play through.

The corpus is tests/golden/render/presets/: `<case>.xml` (or `.XML`), optionally `<case>.notes` (default.notes
otherwise), `<case>.wav` and `<case>.2.wav` (staged as SAMPLES/WT.WAV and WT2.WAV, which the preset names). A case is
named by its path under the corpus without the extension, e.g. `factory/000 Rich Saw Bass`. The cases under `stereo/`
render with headphones plugged in, so in stereo (RENDER_STEREO); the rest render in mono, as through the speaker.

RENDER_PRESET_FILTER takes a comma-separated list of substrings, and runs the cases whose name holds any of them.
RENDER_PRESET_CORPUS=<dir> renders the cases of another copy of the corpus (e.g. its presets upgraded) against the same
records.
RENDER_PRESET_KEEP=<dir> keeps each case's output there, as `<case>.pcm` (interleaved little-endian i32, the case's
length exactly), for tests/golden/render/compare_renders.py.
RENDER_PARAM_DUMP_DIR=<dir> dumps each case's matrix passes and voice sources there, as `<case>.params`
(src/deluge/processing/sound/param_dump.h; read by tools/audible_window/dump.py).
RENDER_RANDOM_SEED seeds the synth's random stream at each case's first note instead of its boot seed, to measure how
much a case depends on its random draws (audio/host.rs).
`./dbt test --update golden-synth` records the cases run instead of comparing them, and drops the record of any case no
longer in the corpus (RENDER_PRESET_FILTER narrows it, and then keeps every record it did not render;
RENDER_PRESET_CORPUS cannot be re-recorded, since the records are the committed corpus's); a changed hash is a
finding to explain in the commit that records the new one.

Gate golden-synth:
    every synth golden master must render as recorded: each of the 453 presets (tests/golden/render/presets:
    synthesized cases for each oscillator, filter, envelope and modulation path, the voice's own note-change, release,
    sample-cache and zone-change paths, the factory and community preset packs, sample and wavetable playback, and live
    input, pitch-shifted up and down, in stereo, with unison and linear interpolation, through unity, in a chord, and on
    a percussive test signal) is loaded by the blank boot song, played by a DIN note script and rendered in real time on
    the audio tier, and its output is compared with its record in tests/golden/render/presets.json (RP-PCM). A case
    fails if its preset did not replace the default synth (RP-LOADED), if the app did not read every scripted event
    (RP-MIDI), if CPU direness rose and the render took reduced-quality paths (RP-RENDER), if the render made a
    synchronous card call (RP-TIER-SYNC-AUDIO), if it rendered silence (RP-SILENT), or if a case whose sample zone is to
    move mid-note never moved it (RP-ZONE). RENDER_PRESET_FILTER runs a subset; ./dbt test --update golden-synth
    re-records.

Gate golden-synth-arm:
    the synth golden masters on 32-bit ARM: every golden-synth case, rendered by golden_vt_render cross-built for armv7
    Linux (the host app compiled by clang for a Cortex-A9 with NEON, as the firmware is) and run under qemu, must match
    the same records in tests/golden/render/presets.json as the x86 build. x86 renders NEON code through SIMDe and
    ARM through the real instructions, so a difference is a vectorised kernel that does not compute what its portable
    form does, or code that depends on the word size.
"""

import concurrent.futures
import importlib.util
import json
import os
import re
import shutil
from pathlib import Path

import golden_update
from scenario_gate import int_field

RECORDS = Path("tests/golden/render/presets.json")

FRAMES_PER_SECOND = 44100
DEFAULT_SECONDS = 2.0
# Per-case capture lengths, for the cases whose sound outlasts the default.
SECONDS = {"env_shape": 3.0, "sample/ts_long_hold": 4.0}
# Cases whose sources read the audio input, and the harness test signal each is fed instead of silence: 1 the
# sawtooth, 2 the percussive signal (noise bursts over a stepped-period saw).
INPUT = {
    "input_mono": 1,
    "input_stereo": 1,
    "input_pitchshift": 1,
    "input_pitchshift_down12": 1,
    "input_pitchshift_down5": 1,
    "input_pitchshift_up_half": 1,
    "input_pitchshift_up7": 1,
    "input_pitchshift_up24": 1,
    "input_pitchshift_linear": 1,
    "input_pitchshift_glide": 1,
    "input_pitchshift_perc_up7": 2,
    "input_pitchshift_perc_down12": 2,
    "input_pitchshift_chord": 2,
    "stereo/input_pitchshift_stereo_up7": 2,
    "stereo/input_pitchshift_unison": 1,
}
# Cases under this directory render with headphones plugged in, so the app renders in stereo; every other case renders
# in mono, as through the device's speaker.
STEREO_DIR = "stereo/"
# Cases whose osc 1 zone end moves mid-note, as the sample marker editor moves it: "<captured block> <new end frame>".
ZONE_END = {"voice/zone_end_loop": "60 2048", "voice/zone_end_once": "50 1024"}
# Cases that change a setting mid-render, as its menu does: "<captured block> <settings path> <value>".
SETTING = {
    "voice/type_change_mono_saw_square": "150 osc1/type square",
    "voice/type_change_mono_saw_supersaw": "150 osc1/type supersaw",
}
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
    if dump_dir := os.environ.get("RENDER_PARAM_DUMP_DIR"):
        dump = Path(dump_dir) / f"{name}.params"
        dump.parent.mkdir(parents=True, exist_ok=True)
        env["RENDER_PARAM_DUMP"] = str(dump)
    if name in INPUT:
        env["RENDER_INPUT"] = str(INPUT[name])
    if name.startswith(STEREO_DIR):
        env["RENDER_STEREO"] = "1"
    if name in ZONE_END:
        env["RENDER_ZONE_END"] = ZONE_END[name]
    if name in SETTING:
        env["RENDER_SETTING"] = SETTING[name]
    return env


def kept_records(records: dict, corpus_cases: list[str]) -> dict:
    """The records a whole-corpus update keeps: those of the cases still in the corpus. A deleted case's record goes
    with it, where a filtered update keeps every record it did not render."""
    return {name: record for name, record in records.items() if name in corpus_cases}


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
        or gate.root / "tests/golden/render/presets"
    )
    records_path = gate.root / RECORDS
    update = golden_update.updating(gate.env)
    if update and gate.env.get("RENDER_PRESET_CORPUS"):
        gate.fail(
            "RP-PCM — the records are the committed corpus's: a RENDER_PRESET_CORPUS run cannot re-record them"
        )
        return
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
    if update and not needles:
        records = kept_records(records, selected)
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
        if name in ZONE_END and result.last("RP-ZONE ") is None:
            gate.fail(f"RP-ZONE {name}: the zone end never moved")
            failed += 1
            continue
        if name in SETTING and result.last("RP-SETTING ") is None:
            gate.fail(f"RP-SETTING {name}: the setting never changed")
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
                f"RP-PCM {name}: no record; ./dbt test --update golden-synth records one"
            )
        elif records[name] != record:
            gate.fail(
                f"RP-PCM {name}: the rendered output changed: recorded {records[name]}, rendered {record}"
            )
        if len(gate.failures) != before:
            failed += 1

    _check_learn(gate, corpus, pcm_record)

    if update and not gate.failures:
        golden_update.write_json(
            records_path, records, str(RECORDS), indent=1, env=gate.env
        )
    gate.say(f"RP-CASES run={len(selected)} passed={len(selected) - failed}")
