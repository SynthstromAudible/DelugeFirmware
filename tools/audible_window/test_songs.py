"""The audible window's song set: the fixed song, then the corpus songs with the most cables, kit rows and automated
params, one each; songs in subdirectories are named by their path under SONGS/."""

import json
import tempfile
import unittest
from pathlib import Path

import songs

AUTOMATED = "0x4A000000" + "2E000000"


class Pick(unittest.TestCase):
    def test_takes_the_fixed_songs_then_one_song_per_count(self):
        with tempfile.TemporaryDirectory() as tmp:
            corpus = Path(tmp) / "SONGS"
            corpus.mkdir()
            write = lambda name, data: (corpus / f"{name}.json").write_text(
                json.dumps(data)
            )
            write("Glitchy_2", {})
            write(
                "cables",
                {
                    "sound": {
                        "cables": [
                            {"patchCable": {"amount": "0x10000000"}} for _ in range(5)
                        ]
                    }
                },
            )
            (corpus / "deep").mkdir()
            write("deep/kit", {"kit": {"soundSources": [{}, {}, {}, {}]}})
            write(
                "auto",
                {
                    "a": AUTOMATED,
                    "b": {"value": AUTOMATED},
                    "c": [AUTOMATED],
                    "noteDataWithSplitProb": AUTOMATED + AUTOMATED,
                },
            )
            write(
                "plain",
                {"patchCable": {}, "kit": {"soundSources": [{}]}, "v": "0x10000000"},
            )
            self.assertEqual(
                songs.pick(corpus), ["Glitchy_2", "cables", "deep/kit", "auto"]
            )

    def test_counts_ignore_note_data(self):
        counts = songs.counts(
            {"noteDataWithSplitProb": AUTOMATED, "x": {"noteData": AUTOMATED}}
        )
        self.assertEqual(counts["automated"], 0)


if __name__ == "__main__":
    unittest.main()
