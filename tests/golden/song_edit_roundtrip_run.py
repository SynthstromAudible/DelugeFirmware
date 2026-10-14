#!/usr/bin/env python3
"""Edit-aware round-trip runner. Runs the song_edit_roundtrip driver over the BYO song corpus: load, a DOM-first
edit of a song-global param, save, reload, and require the edit persisted -- plus the DOM binding and
id-addressing checks it makes on the way. The driver is self-checking (exit 0 = persisted, 1 = load error,
3 = a check failed), so there is no baseline to diff.

The driver is the song_edit_roundtrip scenario of golden_vt_render (driver.py); `./dbt test song-edit-roundtrip`
runs this.

    export DELUGE_SONG_CORPUS=~/path/to/SONGS
    python3 tests/golden/song_edit_roundtrip_run.py
"""

import sys

import driver


def main(argv=None):
    return driver.self_checking_sweep("song_edit_roundtrip", argv, description=__doc__)


if __name__ == "__main__":
    sys.exit(main())
