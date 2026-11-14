"""Serializer agreement runner. Runs the serializer_check driver over the BYO song corpus; the driver is self-checking
(exit 0 = the serializers agree, 1 = load error, 3 = they differ), so there is no baseline to diff.

The driver is the serializer_check scenario of golden_vt_render (driver.py); `./dbt test serializer-check` runs this.

    export DELUGE_SONG_CORPUS=~/path/to/SONGS
    python3 tests/integration/corpus/serializer_check_run.py

Gate serializer-check:
    the serializers agree: for every output of every corpus song, the tree ValueSerializer builds equals the tree the
    JSON writer's text parses to, as for a fixed set of writes
"""

import sys

import driver


def main(argv=None):
    return driver.self_checking_sweep("serializer_check", argv, description=__doc__)


if __name__ == "__main__":
    sys.exit(main())
