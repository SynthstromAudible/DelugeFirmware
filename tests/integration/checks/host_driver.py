"""The corpus host drivers: each gate runs its tests/golden runner over the song corpus, against golden_vt_render.

The drivers (tests/integration/scenarios/drivers/) were once executables of their own; their runners in tests/golden
still own what each one checks -- fixture hashes, stdout baselines, or the driver's own verdict per song -- and drive
golden_vt_render one song per run through tests/golden/driver.py. This module runs the entry's runner with the gate's
binary (and its wrapper, for a cross-built one) and the song corpus (driver.default_corpus(): DELUGE_SONG_CORPUS,
else DELUGE_BACKUP's SONGS, else ~/Deluge Backup/SONGS), prints what it printed, and fails when it does. A runner that
finds nothing to check (exit 77) fails too: the registry entry requires the corpus, so finding none is a broken setup,
not a skip.
"""

import contextlib
import importlib
import io
import sys

# Scenario -> the tests/golden runner that checks it.
RUNNERS = {
    "audio_clip_edit_diff": "audio_clip_edit_run",
    "cable_decode": "cable_decode_run",
    "clip_clone_roundtrip": "clip_clone_run",
    "edit_undo_redo": "edit_undo_redo_run",
    "engine_settings": "engine_settings_run",
    "instrument_codec_decode": "instrument_codec_decode_run",
    "instrument_codec_encode": "instrument_codec_run",
    "instrument_state_roundtrip": "instrument_state_roundtrip_run",
    "json_roundtrip": "json_roundtrip_run",
    "model_decode_diff": "model_decode_diff_run",
    "param_record_diff": "param_record_run",
    "preset_roundtrip": "presets_run",
    "serializer_check": "serializer_check_run",
    "song_edit_roundtrip": "song_edit_roundtrip_run",
    "song_roundtrip": "songs_run",
}


def run(gate):
    golden = str(gate.root / "tests" / "golden")
    if golden not in sys.path:
        sys.path.insert(0, golden)
    driver = importlib.import_module("driver")
    runner = importlib.import_module(RUNNERS[gate.entry.scenario])

    argv = ["--binary", str(gate.binary)]
    if wrapper := gate.env.get("GOLDEN_VT_RENDER_WRAPPER", ""):
        argv += ["--wrapper", wrapper]
    if corpus := driver.default_corpus():
        argv += ["--corpus", corpus]

    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        code = runner.main(argv)
    for line in out.getvalue().splitlines():
        gate.say(line)
    if code == 77:
        gate.fail(
            f"{gate.entry.scenario}: no corpus songs to check (set DELUGE_SONG_CORPUS)"
        )
    elif code != 0:
        gate.fail(f"{gate.entry.scenario}: the runner exited {code}")
