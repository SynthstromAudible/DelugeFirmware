#!/usr/bin/env python3
"""JSON round-trip runner. Runs the json_roundtrip driver over the BYO song corpus; the driver is self-checking
(exit 0 = settled JSON fixed point, 1 = XML/JSON load error, 3 = JSON fixed-point mismatch), so there is
no baseline to diff. Proves the JSON side of the record-first deserializer (JsonSerializer + parseJson +
ValueTreeDeserializer JSON mode) is a faithful round trip.

The driver is the json_roundtrip scenario of golden_vt_render (driver.py); `./dbt test json-roundtrip` runs this.

    export DELUGE_SONG_CORPUS=~/path/to/SONGS
    python3 tests/golden/json_roundtrip_run.py
"""

import sys

import driver


def main(argv=None):
    return driver.self_checking_sweep("json_roundtrip", argv, description=__doc__)


if __name__ == "__main__":
    sys.exit(main())
