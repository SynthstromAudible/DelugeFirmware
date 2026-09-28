"""The render-song gate: a song played as the device plays it, rendered on the audio tier.

`render_song` loads the song through the real load browser, plays it in real time, and reports each stage with the
evidence that it happened (tests/integration/scenarios/render_song_scenario.h lists the lines). The scenario itself
fails a run whose song did not load, whose playback stopped, or whose render made a synchronous card call. This module
adds what depends on the song, and why each check is here:
    RS-VOICES audio   sample voices rendered on the audio tier. A sample-backed song that renders none played
                      nothing, and every other check would pass on silence.
    RS-STREAM reads   card reads while playing. None means the samples were already resident, so nothing streamed.
    RS-UNDERRUN       none. The harness's card answers at once, so an underrun here is a loader fault, not a slow card.
    RS-PCM            the rendered output, against its record in tests/integration/golden/songs.json: the same
                      record the synth golden masters keep (sha256, frames, peak, rms_db).

The song is generated for each run by tests/integration/golden/songs/make_render_song.py, so the card holds exactly
the bytes the record was made from. RENDER_SONG_UPDATE=1 records the output instead of comparing it; a changed hash
is a finding to explain in the commit that records the new one.
"""

import hashlib
import json
import math
import os
import struct
import subprocess
import sys

from scenario_gate import int_field

SONG = "SONGS/RS_STREAM.XML"
RECORD = "RS_STREAM"


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


def run(gate):
    tree = gate.tmp / "card"
    pcm_path = gate.tmp / "out.pcm"
    made = subprocess.run(
        [
            sys.executable,
            str(gate.root / "tests/integration/golden/songs/make_render_song.py"),
            str(tree),
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    if made.returncode != 0:
        gate.fail(f"generating the song failed: {made.stderr.strip()}")
        return

    result = gate.run(
        env={
            "RENDER_TREE": str(tree),
            "RENDER_SONG": SONG,
            "RENDER_PCM_OUT": str(pcm_path),
        }
    )
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

    record = _pcm_record(pcm_path.read_bytes())
    gate.say(
        f"RS-PCM sha256={record['sha256']} frames={record['frames']} peak={record['peak']} rms_db={record['rms_db']}"
    )
    records_path = gate.root / "tests/integration/golden/songs.json"
    records = json.loads(records_path.read_text()) if records_path.exists() else {}
    if os.environ.get("RENDER_SONG_UPDATE") == "1":
        records[RECORD] = record
        records_path.write_text(json.dumps(records, indent=1, sort_keys=True) + "\n")
        gate.say(
            f"RS-PCM recorded as {RECORD} in {records_path.relative_to(gate.root)}"
        )
        return
    expected = records.get(RECORD)
    if expected is None:
        gate.fail(
            f"RS-PCM — no record for {RECORD}; run with RENDER_SONG_UPDATE=1 to make one"
        )
    elif expected != record:
        gate.fail(
            f"RS-PCM — the rendered output changed: recorded {expected}, rendered {record}"
        )
