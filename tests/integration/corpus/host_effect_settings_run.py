#!/usr/bin/env python3
"""Host effect settings runner. Runs the host_effect_settings driver over the song corpus; the driver is self-checking
(exit 0 = every kit's, audio track's and master's effect setting changes by its Edit coherently, 1 = the song does not
load, 3 = a check fails), so there is no baseline to diff.

The driver is the host_effect_settings scenario of golden_vt_render (driver.py); `./dbt test host-effect-settings` runs
this.

    export DELUGE_SONG_CORPUS=~/path/to/SONGS
    python3 tests/integration/corpus/host_effect_settings_run.py

Gate host-effect-settings:
    a host owner's effects go through its chain: for every kit, audio track and master of every corpus song, every legal
    value of every effect setting is a SetEffectSetting after which the owner reads it back, each chain slot is the
    encoding of the owner's state and the render copy is the state; a tick-magnitude change leaves the delay's and
    sidechain's sync levels where they were; a changed filter mode and compressor attack survive a save and reload
"""

import sys

import driver


def main(argv=None):
    return driver.self_checking_sweep("host_effect_settings", argv, description=__doc__)


if __name__ == "__main__":
    sys.exit(main())
