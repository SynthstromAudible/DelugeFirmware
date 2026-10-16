#! /usr/bin/env python3
"""Unit tests for check_setting_menus.py's song-state writes.

Run directly with:
    python3 scripts/tasks/test_check_setting_menus.py
"""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import check_setting_menus as csm

#: One planted write per kind of song-state write a gesture may not make.
PLANTED = {
    "reverb record": "project.reverb.room_size = 1;",
    "reverb engine": "AudioEngine::reverb.setRoomSize(v);",
    "reverb pan": "AudioEngine::reverbPan = v;",
    "sidechain": "AudioEngine::reverbSidechainSettings.attack = v;",
    "swing interval": "currentSong->changeSwingInterval(v);",
    "disabled scale": "song->project.scale.disabled_preset_scales[i] = true;",
    "raw device field": "instrument->collapseMPE = true;",
    "device setter": "instrument->setCollapseAftertouch(true);",
    "channel": "instrument->setChannel(4);",
    "name": 'output->setName("X");',
    "raw name": 'output->name = "X";',
    "direction": "row->setSequenceDirectionMode(s, d);",
    "audio clip setter": "clip->setSampleStartPos(0);",
    "audio clip holder": "clip->sampleHolder.endPos = 5;",
    "clip name": 'clip->name = "X";',
    "clip direction": "clip->setSequenceDirectionMode(stack, mode);",
    "midi program": "clip->midiPGM = 5;",
    "bend range": "expression->bendRanges[0] = 12;",
    "sync scaling clip": "song->setInputTickScaleClip(clip);",
    "audio source": "audioOutput->setOutputRecordingFrom(track);",
    "audio source cleared": "audioOutput->clearRecordingFrom();",
    "audio channel": "audioOutput->inputChannel = AudioInputChannel::LEFT;",
    "audio mode": "ao->mode = AudioOutputMode::looper;",
    "audio mode cast": "ao->mode = static_cast<AudioOutputMode>(value);",
    "cable polarity": "set->patchCables[i].polarity = Polarity::BIPOLAR;",
    "hide unlabeled": "instrument->hideUnlabeledCC = true;",
    "cc label": "instrument->setNameForCC(cc, name);",
}


class SongStateWrites(unittest.TestCase):
    def check(self, line: str) -> list[str]:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "planted.cpp").write_text(f"void gesture() {{\n\t{line}\n}}\n")
            return csm.song_state_writes((root,), allowed={})

    def test_each_planted_write_is_reported(self):
        for what, line in PLANTED.items():
            with self.subTest(what):
                self.assertEqual(len(self.check(line)), 1, line)

    def test_the_edits_and_reads_are_not(self):
        for line in (
            'deluge::edits::songSettingFor("reverb/roomSize", v);',
            'deluge::edits::trackSettingFor(*output, "name", v);',
            "AudioEngine::reverbSidechainSettings.sync_type = t;",
            "const float size = AudioEngine::reverb.getRoomSize();",
            "if (output->name == other) {}",
            "PresetRequest r{.name = initial};",
            "set->setPolarity(index, polarity);",
            "const auto mode = ao->scrolledMode(offset);",
        ):
            with self.subTest(line):
                self.assertEqual(self.check(line), [])

    def test_an_allowed_file_is_not_reported(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "loader.cpp").write_text("output->setName(n);\n")
            self.assertEqual(
                csm.song_state_writes(
                    (root,), allowed={"loader.cpp": {"": "a load path"}}
                ),
                [],
            )


if __name__ == "__main__":
    unittest.main()
