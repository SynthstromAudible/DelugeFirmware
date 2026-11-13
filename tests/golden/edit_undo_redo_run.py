#!/usr/bin/env python3
"""M1d Edit undo/redo hook runner. Runs the edit_undo_redo driver over the BYO song corpus; the driver is
self-checking (exit 0 = pass, 1 = load error, 3 = a check failed), so there is no baseline to diff.

The driver is the edit_undo_redo scenario of golden_vt_render (driver.py); `./dbt test edit-undo-redo` runs this.

    export DELUGE_SONG_CORPUS=~/path/to/SONGS
    python3 tests/golden/edit_undo_redo_run.py

Gate edit-undo-redo:
    every reified Edit applied to a loaded corpus song must resolve through a live DOM address and come back exactly
    through the real undo and redo (ActionLogger::revert): the driver applies each Edit kind and checks the state after
    apply, undo and redo (self-checking; a song that does not load is skipped)
"""

import sys

import driver


def main(argv=None):
    return driver.self_checking_sweep("edit_undo_redo", argv, description=__doc__)


if __name__ == "__main__":
    sys.exit(main())
