"""The render-song gate: a song played as the device plays it, rendered on the audio tier.

`render_song` loads the song through the real load browser, plays it in real time, and reports each stage with the
evidence that it happened (tests/integration/scenarios/render_song_scenario.h lists the lines). The scenario itself
fails a run whose song did not load, whose playback stopped, or whose render made a synchronous card call. This module
adds what depends on the song, and why each check is here:
    RS-VOICES audio   sample voices rendered on the audio tier. A sample-backed song that renders none played
                      nothing, and every other check would pass on silence.
    RS-STREAM reads   card reads while playing. None means the samples were already resident, so nothing streamed.
    RS-UNDERRUN       none. The harness's card answers at once, so an underrun here is a loader fault, not a slow card.
    RS-CAPTURE        the width the gate asked for: stereo with RENDER_STEREO=1 (headphones plugged in), mono otherwise.
                      A scenario that ignored the variable would render the other width and record it.
    RS-SWELL          the generated song only: its samples swell and fade twice in their six seconds, so the output's
                      level must move. Playback stuck at the start of each sample renders a steady level, streams
                      nothing past the first cluster, and passes every other check here -- the card reads it counts
                      are the load's own background reads.
    RS-MODFX-SWITCH   a gate that switches mod FX types (RENDER_MODFX_SWITCH) only: the scenario's summary must show
                      every switch asked for applied, setting the types asked for in order, and each owner's own marker
                      must show what it holds: a ring type a mod FX ring, the grain a granular ring, the phaser none. A hook that never ran, or switched to another type, would
                      leave the song rendering as some other song does and still compare with a record made the same way.
    RS-MODFX-CLEAR    a gate that clears mod FX memory (RENDER_MODFX_CLEAR) only: every clear asked for applied; a gate
                      whose clears reach no grain must reach both of clearMemory's paths, a ring zeroed and, with none
                      held, the phaser's memories; one that clears a grain must clear only grains.
    RS-SWITCH         a gate that switches delays or mod FX types, or clears mod FX memory, mid-render
                      (RENDER_DELAY_SWITCH, RENDER_MODFX_SWITCH, RENDER_MODFX_CLEAR) only: the same song rendered
                      unswitched must render differently, or the switches never reached the render.
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

from scenario_gate import field, int_field

# The scenario's mid-render switches; a gate that sets any of them is compared with the song rendered without them.
SWITCHES = ("RENDER_DELAY_SWITCH", "RENDER_MODFX_SWITCH", "RENDER_MODFX_CLEAR")


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


def _check_width(gate, result, label: str) -> None:
    """The run captured the width the gate asked for (see RS-CAPTURE)."""
    wanted = 1 if gate.env.get("RENDER_STEREO") == "1" else 0
    stereo = int_field(result.last("RS-CAPTURE "), "stereo")
    if stereo is None:
        gate.fail(f"RS-CAPTURE — {label}'s capture line reports no stereo= field")
    elif stereo != wanted:
        gate.fail(
            f"RS-CAPTURE — {label} rendered {'stereo' if stereo else 'mono'}, asked for {'stereo' if wanted else 'mono'}"
        )


# The mod FX types, as a file writes them, that run over a ring (effects::is_ring_type).
RING_TYPES = frozenset({"chorus", "StereoChorus", "dimension", "flanger", "TapeWarble"})
# The granular mod FX type, which holds the granular ring in place of a mod FX ring.
GRAIN_TYPE = "grainFX"


def _check_mod_fx_hooks(gate, result) -> None:
    """The mod FX switches and clears the gate asked for were applied and reached their paths (see RS-MODFX-SWITCH and
    RS-MODFX-CLEAR)."""
    switches = gate.env.get("RENDER_MODFX_SWITCH")
    if switches:
        types = [item.split(":")[1] for item in switches.split(",")]
        line = result.last("RS-MODFX-SWITCH applied=")
        owners = result.starting("RS-MODFX-SWITCH block=")
        if line is None:
            gate.fail("RS-MODFX-SWITCH — no summary line: the switch hook did not run")
            return
        # Each owner's own marker says what it holds after its switch: a mod FX ring, a granular ring, or neither.
        held = [
            (field(o, "type"), int_field(o, "ring"), int_field(o, "grain"))
            for o in owners
        ]
        wanted = [(t, int(t in RING_TYPES), int(t == GRAIN_TYPE)) for t in types]
        ring = sum(h[1] == 1 for h in held)
        grain = sum(h[2] == 1 for h in held)
        if (
            int_field(line, "applied") != len(types)
            or field(line, "types") != ",".join(types)
            or held != wanted
            or int_field(line, "ring") != ring
            or int_field(line, "grain") != grain
            or int_field(line, "noring") != len(held) - ring - grain
        ):
            gate.fail(
                f"RS-MODFX-SWITCH — asked for {','.join(types)}, the owners held {held}, summary: {line}"
            )
    clears = gate.env.get("RENDER_MODFX_CLEAR")
    if clears:
        asked = len(clears.split(","))
        line = result.last("RS-MODFX-CLEAR applied=")
        if line is None:
            gate.fail("RS-MODFX-CLEAR — no summary line: the clear hook did not run")
        elif int_field(line, "grain"):
            if int_field(line, "applied") != asked or int_field(line, "grain") != asked:
                gate.fail(
                    f"RS-MODFX-CLEAR — asked for {asked} clears of a grain, applied: {line}"
                )
        elif (
            int_field(line, "applied") != asked
            or not int_field(line, "ring")
            or not int_field(line, "noring")
        ):
            gate.fail(
                f"RS-MODFX-CLEAR — asked for {asked} clears reaching a ring and the phaser's memories, applied: {line}"
            )


def _differs_unswitched(gate, song: str | None, generated: str, pcm: bytes) -> bool:
    """Render the song again with none of its switches, and fail unless it differs from @p pcm (see RS-SWITCH). False when
    that run itself failed."""
    env = {} if song else _generate(gate, generated)
    if env is None:
        return False
    path = gate.tmp / "unswitched.pcm"
    env["RENDER_PCM_OUT"] = str(path)
    for switch in SWITCHES:
        env[switch] = ""
    result = gate.run(env=env)
    if result.code != 0 or not result.has_line(gate.entry.result):
        gate.fail("RS-SWITCH — the unswitched render failed")
        gate.show(result)
        return False
    if path.read_bytes() == pcm:
        gate.fail(
            "RS-SWITCH — the switches changed nothing: the song renders as it does unswitched"
        )
    else:
        gate.say("RS-SWITCH the switched render differs from the unswitched one")
    return True


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
    _check_width(gate, result, record_name)
    _check_mod_fx_hooks(gate, result)

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
    if any(gate.env.get(switch) for switch in SWITCHES):
        if not _differs_unswitched(gate, song, generated, pcm):
            return
        dry = None
    else:
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
        dry_result = gate.run(env=dry_env)
        if not gate.require_standard(dry_result):
            return
        _check_width(gate, dry_result, dry)
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
