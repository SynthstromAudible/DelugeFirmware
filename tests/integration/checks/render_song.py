"""The render-song gate: a song played as the device plays it, rendered on the audio tier.

`render_song` loads the song through the real load browser, plays it in real time, and reports each stage with the
evidence that it happened (tests/integration/scenarios/render_song_scenario.h lists the lines). The scenario itself
fails a run whose song did not load, whose playback stopped, or whose render made a synchronous card call. This module
adds what depends on the song, and why each check is here:
    RS-VOICES audio   sample voices rendered on the audio tier. A sample-backed song that renders none played
                      nothing, and every other check would pass on silence.
    RS-STREAM reads   card reads while playing. None means the samples were already resident, so nothing streamed.
    RS-UNDERRUN       none. The harness's card answers at once, so an underrun here is a loader fault, not a slow card.
    RS-SWELL          the generated song only: its samples swell and fade twice in their six seconds, so the output's
                      level must move. Playback stuck at the start of each sample renders a steady level, streams
                      nothing past the first cluster, and passes every other check here -- the card reads it counts
                      are the load's own background reads.
    RS-PCM            the rendered output, against its record in tests/integration/golden/songs.json: the same
                      record the synth golden masters keep (sha256, frames, peak, rms_db).

By default the song is generated for each run by tests/integration/golden/songs/make_render_song.py, so the card holds
exactly the bytes the record was made from. An entry that names its own RENDER_SONG plays that song from its
GOLDEN_FIXTURE corpus card instead (render-song-corpus). Either way the record is keyed by the song's name.
RENDER_SONG_UPDATE=1 records the output instead of comparing it; a changed hash is a finding to explain in the commit
that records the new one.
"""

import hashlib
import importlib.util
import json
import math
import os
import struct
import subprocess
import sys

from scenario_gate import int_field


def _pcm_record(pcm: bytes) -> dict:
    """The output's record: its hash, length, peak and RMS level (dB below full scale)."""
    count = len(pcm) // 4
    samples = struct.unpack(f"<{count}i", pcm[: count * 4])
    peak = max((abs(s) for s in samples), default=0)
    mean_square = sum(s * s for s in samples) / count if count else 0.0
    rms_db = (
        20 * math.log10(math.sqrt(mean_square) / 2**31) if mean_square else -math.inf
    )
    return {
        "sha256": hashlib.sha256(pcm).hexdigest(),
        "frames": count // 2,
        "peak": peak,
        "rms_db": round(rms_db, 2),
    }


def _generate(gate, name: str = "RS_STREAM") -> dict[str, str] | None:
    """Generate song @p name's card tree; the environment that plays it, or None when generating failed."""
    tree = gate.tmp / f"card-{name}"
    made = subprocess.run(
        [
            sys.executable,
            str(gate.root / "tests/integration/golden/songs/make_render_song.py"),
            str(tree),
            "--song",
            name,
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    if made.returncode != 0:
        gate.fail(f"generating the song failed: {made.stderr.strip()}")
        return None
    return {"RENDER_TREE": str(tree), "RENDER_SONG": f"SONGS/{name}.XML"}


def generated_song(env: dict[str, str]) -> str:
    """The generated song a gate plays: its own `RENDER_GENERATED` (the registry's, over the shell's, in @p env), else
    RS_STREAM."""
    return env.get("RENDER_GENERATED") or "RS_STREAM"


def dry_twin(name: str, songs) -> str | None:
    """The dry twin of generated song @p name among @p songs (RS_KIT_FX -> RS_KIT_DRY, RS_KIT_FILTERS ->
    RS_KIT_FILTERS_DRY), or None for RS_STREAM, which plays no effect. Any other song without a twin raises
    LookupError: the twin is how its gate shows the effects reach the render."""
    if name == "RS_STREAM":
        return None
    for twin in (name.removesuffix("_FX") + "_DRY", name + "_DRY"):
        if twin != name and twin in songs:
            return twin
    raise LookupError(f"{name} has no dry twin in make_render_song.py")


def _dry_twin(gate, name: str) -> str | None:
    """The generator's dry twin of generated song @p name (see dry_twin)."""
    path = gate.root / "tests/integration/golden/songs/make_render_song.py"
    spec = importlib.util.spec_from_file_location("make_render_song", path)
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    return dry_twin(name, generator.SONGS)


def _level_swing(pcm: bytes) -> float:
    """The ratio of the loudest to the quietest quarter second over the first 5.5 s of the left channel."""
    count = len(pcm) // 4
    left = struct.unpack(f"<{count}i", pcm[: count * 4])[0::2]
    window = 44100 // 4
    levels = [
        math.sqrt(sum(x * x for x in left[i : i + window]) / window)
        for i in range(0, min(len(left), 44100 * 11 // 2) - window + 1, window)
    ]
    quietest = min(levels, default=0.0)
    return max(levels, default=0.0) / quietest if quietest > 0 else math.inf


def run(gate):
    pcm_path = gate.tmp / "out.pcm"
    song = gate.env.get("RENDER_SONG")
    generated = generated_song(gate.env)

    env = {} if song else _generate(gate, generated)
    if env is None:
        return
    record_name = song.rsplit("/", 1)[-1].rsplit(".", 1)[0] if song else generated
    env["RENDER_PCM_OUT"] = str(pcm_path)
    result = gate.run(env=env)
    if not gate.require_standard(result):
        return
    gate.show(result)

    voices = int_field(result.last("RS-VOICES "), "audio")
    if voices is None or voices <= 0:
        gate.fail(
            f"RS-VOICES — no sample voice rendered on the audio tier ({result.last('RS-VOICES ')})"
        )
    reads = int_field(result.last("RS-STREAM "), "reads")
    if reads is None or reads <= 0:
        gate.fail("RS-STREAM — no card read while playing: nothing streamed")
    underrun = result.last("RS-UNDERRUN ")
    if int_field(underrun, "wait") != 0 or int_field(underrun, "unassign") != 0:
        gate.fail(
            f"RS-UNDERRUN — playback missed sample data at a card that answers at once ({underrun})"
        )

    pcm = pcm_path.read_bytes()
    try:
        dry = None if song else _dry_twin(gate, generated)
    except LookupError as missing:
        gate.fail(f"RS-FX — {missing}")
        return
    if dry:
        dry_env = _generate(gate, dry)
        if dry_env is None:
            return
        dry_path = gate.tmp / "dry.pcm"
        dry_env["RENDER_PCM_OUT"] = str(dry_path)
        if not gate.require_standard(gate.run(env=dry_env)):
            return
        if dry_path.read_bytes() == pcm:
            gate.fail(
                f"RS-FX — the effects changed nothing: {generated} renders as {dry}"
            )
        else:
            gate.say(f"RS-FX {generated} differs from {dry}")
    if not song and generated == "RS_STREAM":
        swing = _level_swing(pcm)
        gate.say(f"RS-SWELL ratio={swing:.2f}")
        # 1.62 when playback advances; 1.02 when it is stuck at the samples' start.
        if swing < 1.25:
            gate.fail(
                f"RS-SWELL — the output's level barely moves (ratio {swing:.2f}): playback is not advancing through the samples"
            )

    record = _pcm_record(pcm)
    gate.say(
        f"RS-PCM sha256={record['sha256']} frames={record['frames']} peak={record['peak']} rms_db={record['rms_db']}"
    )
    records_path = gate.root / "tests/integration/golden/songs.json"
    records = json.loads(records_path.read_text()) if records_path.exists() else {}
    if os.environ.get("RENDER_SONG_UPDATE") == "1":
        records[record_name] = record
        records_path.write_text(json.dumps(records, indent=1, sort_keys=True) + "\n")
        gate.say(
            f"RS-PCM recorded as {record_name} in {records_path.relative_to(gate.root)}"
        )
        return
    expected = records.get(record_name)
    if expected is None:
        gate.fail(
            f"RS-PCM — no record for {record_name}; run with RENDER_SONG_UPDATE=1 to make one"
        )
    elif expected != record:
        gate.fail(
            f"RS-PCM — the rendered output changed: recorded {expected}, rendered {record}"
        )
