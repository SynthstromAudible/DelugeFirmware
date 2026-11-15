"""The Deluge instrument's decode check: the instrument_codec_decode driver over every corpus song and every preset
beside the corpus (see instrument_codec_run.py).

    export DELUGE_SONG_CORPUS=~/path/to/SONGS
    python3 tests/integration/corpus/instrument_codec_decode_run.py

Gate instrument-codec-decode:
    the Deluge instrument reads its own state as the app's reader does: for every sound of every corpus song and every
    synth and kit preset beside the corpus, the element it was read from, decoded into a new sound's state, is the state
    the reader left (every field the codec reads, older formats included)
"""

import sys

from instrument_codec_run import sweep


def main(argv=None):
    return sweep("instrument_codec_decode", argv, description=__doc__)


if __name__ == "__main__":
    sys.exit(main())
