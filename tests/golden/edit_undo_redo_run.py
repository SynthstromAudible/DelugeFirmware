#!/usr/bin/env python3
"""M1d Edit undo/redo hook runner. Runs the edit_undo_redo driver over the BYO song corpus; the driver is
self-checking (exit 0 = pass, 1 = load error, 3 = a check failed), so there is no baseline to diff.

The driver is the edit_undo_redo scenario of golden_vt_render (driver.py); `./dbt test edit-undo-redo` runs this.

    export DELUGE_SONG_CORPUS=~/path/to/SONGS
    python3 tests/golden/edit_undo_redo_run.py
"""

import sys

import driver


def main(argv=None):
    return driver.self_checking_sweep("edit_undo_redo", argv, description=__doc__)


if __name__ == "__main__":
    sys.exit(main())
