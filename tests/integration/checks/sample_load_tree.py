"""The sample-load tree gate: every holder of a firmware-built song and two presets resolves where its file is.

Builds a song through the firmware (a synth with a sample source and a wavetable source, a kit with a sample drum, and
an audio clip), saves it with collected media, and loads it back through the load screen. Then writes a synth preset
and a kit preset through the firmware serialiser and loads each. Every fixture document is written by the firmware;
none is hand-written.

What the checks cover (tests/integration/scenarios/sample_load_tree_scenario.cpp writes the lines):
    SLT-HOLDER  per holder: its resolved path, whether a file is bound, and where the file was loaded from.
                - The song's samples resolve to their collected copies in the song's folder, because their originals
                  were removed from the card before the load.
                - The audio clip's sample stays resident and is claimed by the load's memory-only pass.
                - Each preset's sample resolves to the preset's own folder, with its path rewritten there.
    SLT-CLIP    the audio clip's name after the load is its sample's path.
    SLT-KIT     an abort raised after the kit preset's first drum binds stops the load: the second drum stays empty.
    SLT-SONG / SLT-SYNTH / SLT-KIT
                how many times each load called routineWithClusterLoading, the cadence that keeps audio running.
    LT          the load trace: one line per call into the traced leaf functions, while a load runs.
    SLT-CRUCIAL a song loaded while playback runs: which holders its crucial-only pass bound (only what the playing
                clips need first), that its armed swap happened, and every holder bound to its collected copy after.
    LTC         the crucial load's trace, kept apart from LT so the LT lines stay comparable with earlier runs.
    SLT-ASYNC   each load again through the async sample load, ticked from the interaction tier with every file it binds
                dropped from memory first: SLTSONG2's crucial and full walks, the synth preset, and the kit preset with
                its abort. Each must end as its blocking load did, with no sample read from the card on the interaction
                tier. Then the song twice more: with the folder's exists probe held until every sample request has
                come back (each miss must wait for it), and with the card ejected while the probe is out (the load must
                end on the unusable card, and a second load on the same context must bind everything once the card is
                back, never taking the old card's answer for the new card's). And the synth preset with the loader held
                past the stall bound: that load must end Failed/Stalled on the bound's walk with nothing bound, and a
                second load must bind the preset's sample once the hold is lifted. Last, a song load dropped with its
                folder probe still out, and a load of only its wavetable on the same context: the wavetable must still
                bind to its collected copy, with no sample left to collect the answer.

The registry entry requires each marker and the result line. This module runs the scenario itself so that, pass or
fail, it can write the run's traces out.

Environment:
    SAMPLE_LOAD_TREE_TRACE          if set, a file to write the run's LT lines to
    SAMPLE_LOAD_TREE_CRUCIAL_TRACE  if set, a file to write the run's LTC lines to
"""

import re


def run(gate):
    result = gate.run()
    for variable, prefix in (
        ("SAMPLE_LOAD_TREE_TRACE", "LT "),
        ("SAMPLE_LOAD_TREE_CRUCIAL_TRACE", "LTC "),
    ):
        if gate.env.get(variable):
            (gate.root / gate.env[variable]).write_text(
                "".join(f"{line}\n" for line in result.starting(prefix))
            )
    if not gate.require_standard(result):
        return
    # The SLT- lines and the tier inventory: the LT/LTA/LTC/LTP trace lines go to the trace files, not the log.
    for line in result.lines:
        if line.startswith("SLT-") or re.match(r"TIER-SYNC(-AT)? ", line):
            gate.say(line)
    gate.say(
        "PASS — every holder resolved where the fixture put its file, the abort stopped the kit load, and the"
    )
    gate.say(
        "       audio clip kept its sample's name; a load while playing bound only what the playing clips need"
    )
    gate.say(
        "       first, and the rest after its armed swap; and each load, again through the async sample load,"
    )
    gate.say(
        "       ended the same way without reading a sample on the interaction tier"
    )
