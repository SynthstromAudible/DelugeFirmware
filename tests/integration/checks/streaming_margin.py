"""The streaming-margin gate: whether sample playback keeps up when storage is as slow as the device's.

Plays the render-song song (tests/integration/golden/songs/make_render_song.py) under a storage model: the fill task
waits `fill_delay_ms` to be scheduled before each pass, and each chunk's read takes `fill_read_us`. The figures are
the device's, measured per interval on the Rust BSP (storage-execution-model design, Appendix C): a 16 ms mean wait
(25 ms at most) in steady playback, 260 ms during a song load, and 5.8 ms to read a 32 KB chunk. On the device about
three quarters of a chunk's latency is that wait, so it is the model's main term, and the card's time is the second.

Three runs, and what each is for:
    steady    the device's steady figures. No underrun: at 16 ms the fill task has most of a cluster's 186 ms of
              playback to spare.
    starved   a 500 ms wait, far past a cluster's playback. Must drop voices: this is the proof the gate can see an
              underrun at all, without which a green steady run would mean nothing.
    burst     the steady figures with a 260 ms wait for half a second of playback, as a song load imposes. Reported,
              not gated: the device drops voices here too, and it becomes a gate when the storage tier gets a
              scheduling guarantee.

With MARGIN_SWEEP set (a comma-separated list of waits in ms) the gate instead prints the underrun count at each
wait -- the margin curve -- and checks nothing.
"""

import importlib.util
from pathlib import Path

from scenario_gate import int_field

STEADY = {"RENDER_FILL_DELAY_MS": "16", "RENDER_FILL_READ_US": "5800"}
STARVED = {"RENDER_FILL_DELAY_MS": "500", "RENDER_FILL_READ_US": "5800"}
# 700 blocks (about 2 s) into playback, 172 blocks (about 0.5 s) long.
BURST = {**STEADY, "RENDER_FILL_BURST": "700:172:260"}


def _render_song():
    """The render-song check module, for its card generation."""
    spec = importlib.util.spec_from_file_location(
        "render_song", Path(__file__).with_name("render_song.py")
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _underruns(result) -> tuple[int | None, int | None]:
    line = result.last("RS-UNDERRUN ")
    return int_field(line, "wait"), int_field(line, "unassign")


def _play(gate, card_env, model, label):
    """One run under `model`; the result, or None when the run itself failed."""
    result = gate.run(env={**card_env, **model})
    if not gate.require_standard(result):
        return None
    wait, unassign = _underruns(result)
    reads = int_field(result.last("RS-STREAM "), "reads")
    gate.say(
        f"SM-{label} {result.last('RS-MODEL ')[len('RS-MODEL ') :]} reads={reads} wait={wait} unassign={unassign}"
    )
    return result


def run(gate):
    card_env = _render_song()._generate(gate)
    if card_env is None:
        return

    sweep = gate.env.get("MARGIN_SWEEP")
    if sweep:
        for delay in sweep.split(","):
            _play(
                gate,
                card_env,
                {"RENDER_FILL_DELAY_MS": delay.strip(), "RENDER_FILL_READ_US": "5800"},
                "SWEEP",
            )
        return

    steady = _play(gate, card_env, STEADY, "STEADY")
    if steady is not None:
        if int_field(steady.last("RS-STREAM "), "reads") in (None, 0):
            gate.fail(
                "SM-STEADY — no card read while playing: nothing streamed, so nothing here measures a margin"
            )
        if _underruns(steady) != (0, 0):
            gate.fail(
                f"SM-STEADY — playback missed sample data at the device's steady figures ({steady.last('RS-UNDERRUN ')})"
            )

    starved = _play(gate, card_env, STARVED, "STARVED")
    if starved is not None and not (_underruns(starved)[1] or 0) > 0:
        gate.fail(
            "SM-STARVED — a 500 ms wait dropped no voice: the gate cannot see an underrun"
        )

    _play(gate, card_env, BURST, "BURST")
