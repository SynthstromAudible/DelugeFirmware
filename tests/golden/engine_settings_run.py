"""A sound's settings through its slot, over every corpus song, every synth and kit preset beside the corpus and the
community presets (which hold MIDI-learned knobs). The engine_settings driver checks each file itself (exit 0 holds,
1 does not load, 3 a check fails), so there is no baseline.

    export DELUGE_SONG_CORPUS=~/path/to/SONGS
    python3 tests/golden/engine_settings_run.py

Gate engine-settings:
    a sound's settings go through its slot: every sound of every corpus song, of every synth and kit preset beside the
    corpus and of the community presets is bound to its slot, as is a synth added to a running song or replacing
    another; every setting the sound editor changes is an Edit, after which the sound still plays its record's state and
    the snapshot equals its projection of it
"""

import os
import sys

from instrument_codec_run import sweep

HERE = os.path.dirname(os.path.abspath(__file__))
COMMUNITY = os.path.join(HERE, "render", "presets", "community")


def main(argv=None):
    extra = [
        (os.path.join(COMMUNITY, n), "SYNTH")
        for n in sorted(os.listdir(COMMUNITY))
        if n.lower().endswith(".xml")
    ]
    return sweep("engine_settings", argv, description=__doc__, extra_jobs=extra)


if __name__ == "__main__":
    sys.exit(main())
