"""The Deluge instrument's decode check: the instrument_codec_decode driver over every corpus song and every preset
beside the corpus (see instrument_codec_run.py).

    export DELUGE_SONG_CORPUS=~/path/to/SONGS
    python3 tests/golden/instrument_codec_decode_run.py
"""

import sys

from instrument_codec_run import sweep


def main(argv=None):
    return sweep("instrument_codec_decode", argv, description=__doc__)


if __name__ == "__main__":
    sys.exit(main())
