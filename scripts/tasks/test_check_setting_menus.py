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
    "arp field": "soundEditor.currentArpSettings->numOctaves = 3;",
    "arp field on a drum": "thisDrum->arpSettings.mode = ArpMode::ARP;",
    "arp field through a getter": "sound->getArpSettings()->noteMode = ArpNoteMode::UP;",
    "arp field through a context": "ctx.arp->randomizerLock = on;",
    "arp preset": "arp.updateSettingsFromCurrentPreset();",
    "clip mute command": "clip->muteMIDICommand = learned;",
    "clip mute command cleared": "clip->muteMIDICommand.clear();",
    "row mute command field": "drum->muteMIDICommand.channelOrZone = 3;",
    "track input": "instrument->midiInput = learned;",
    "lane input cleared": "drum->midiInput.clear();",
    "input cable": "melodicInstrument->midiInput.cable = cable;",
    "knob learned raw": "sound->learnKnob(cable, param, 0, 0, channel, song);",
    "knobs unlearned raw": "modControllable->unlearnKnobs(param, song);",
    "knob list grown": "owner->midi_knobs.push_back(knob);",
    "knob list set": "owner.midi_knobs = std::move(knobs);",
    "knob list cleared": "owner.midi_knobs.clear();",
    "knob in the list": "owner.midi_knobs[0].relative = false;",
    "knob list erased by a function": "std::erase_if(owner->midi_knobs, isStale);",
    "knob list sorted by a function": "std::ranges::sort(owner.midi_knobs, byParam);",
    "section launch command": "section.launch_midi_command = command;",
}


class SoundSettingWrites(unittest.TestCase):
    def check(self, line: str) -> list[str]:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "planted.cpp").write_text(f"void gesture() {{\n\t{line}\n}}\n")
            return csm.calls(root)

    def test_a_raw_write_of_a_voice_or_midi_output_field_is_reported(self):
        for line in (
            "t.sound->polyphonic = static_cast<PolyphonyMode>(v);",
            "t.sound->maxVoiceCount = static_cast<uint8_t>(v);",
            "t.sound->outputMidiChannel = static_cast<uint8_t>(v);",
        ):
            with self.subTest(line):
                self.assertEqual(len(self.check(line)), 1, line)

    def test_the_edit_and_a_read_are_not(self):
        for line in (
            'deluge::edits::settingFor(*t.sound, "maxVoices", v);',
            "if (t.sound->polyphonic != PolyphonyMode::POLY) {}",
        ):
            with self.subTest(line):
                self.assertEqual(self.check(line), [], line)


class SceneFollowUps(unittest.TestCase):
    def check(self, line: str) -> list[str]:
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "planted.cpp").write_text(f"void gesture() {{\n\t{line}\n}}\n")
            return csm.scene_follow_ups(root)

    def test_a_scene_retuning_voices_itself_is_reported(self):
        self.assertEqual(
            len(self.check("t.sound->recalculateAllVoicePhaseIncrements(t.stack);")), 1
        )

    def test_the_edit_is_not(self):
        self.assertEqual(
            self.check('deluge::edits::settingFor(*t.sound, "transpose", v);'), []
        )

    def test_a_scene_raising_the_reverb_beside_a_setting_edit_is_reported(self):
        self.assertEqual(
            len(
                self.check(
                    'deluge::edits::effectSettingFor(*t.mod, "sidechain/attack", v);\n'
                    "\tAudioEngine::mustUpdateReverbParamsBeforeNextRender = true;"
                )
            ),
            1,
        )

    def test_a_param_writes_raise_is_not(self):
        self.assertEqual(
            self.check("AudioEngine::mustUpdateReverbParamsBeforeNextRender = true;"),
            [],
        )


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
            'deluge::edits::arpSettingFor(*ctx.arp, "arpeggiator/rate", v);',
            "if (ctx.arp->mode == ArpMode::OFF) {}",
            "return ctx.arp != nullptr ? ctx.arp->numOctaves : 1;",
            "learnedThing = &whichClip->muteMIDICommand;",
            "if (drum->midiInput.containsSomething()) {}",
            "midiInput.cable = &cable;",
            "(*current_item_)->learnKnob(cable, whichKnob, modKnobMode, midiChannel);",
            "getCurrentMenuItem()->learnKnob(nullptr, whichModEncoder, mode, 255);",
            "MenuItemWithCCLearning::learnKnob(cable, whichKnob, modKnobMode, midiChannel);",
            "void learnKnob(MIDICable* cable, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final {",
            "deluge::edits::learnKnobFor(*owner, param, cable, whichKnob, channel);",
            "for (const MIDIKnob& knob : owner->midi_knobs) {}",
            "if (owner.midi_knobs[0].relative == true) {}",
            "const auto at = std::ranges::find_if(owner.midi_knobs, isPan);",
            "const auto n = std::erase_if(copy, isStale);",
            "// owner.launch_midi_command = command;",
            "const int x = 1; // section.launch_midi_command = command;",
            "/* section.launch_midi_command = command; */",
        ):
            with self.subTest(line):
                self.assertEqual(self.check(line), [])

    def test_a_block_comment_is_not_code(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "documented.cpp").write_text(
                "/**\n * A gesture used to set section.launch_midi_command = command;\n * and owner.midi_knobs.clear();\n */\n"
                "void gesture() { /* output->setName(n); */ int x = 1; }\n"
                "/* opened here\n   and output->setName(n);\n   closed */ void other() {}\n"
            )
            self.assertEqual(csm.song_state_writes((root,), allowed={}), [])
            (root / "documented.cpp").write_text(
                "/* a comment */ owner.midi_knobs.clear();\n"
                "/* opened\n closed */ owner.midi_knobs.clear();\n"
            )
            self.assertEqual(len(csm.song_state_writes((root,), allowed={})), 2)

    def test_the_menu_items_allowance_passes_its_line_and_no_other_learn(self):
        allowed_file = "deluge/gui/menu_item/menu_item_with_cc_learning.cpp"
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            path = root / allowed_file
            path.parent.mkdir(parents=True)
            path.write_text(
                "void learn() {\n"
                "\tcurrentModControllable->learnKnob(cable, whichKnob, mode, channel);\n"
                "}\n"
            )
            self.assertEqual(
                csm.song_state_writes((root,), allowed=csm.SONG_STATE_ALLOWED), []
            )
            path.write_text(
                "void learn() {\n"
                "\tcurrentModControllable->learnKnob(cable, whichKnob, mode, channel);\n"
                "\tsound->learnKnob(cable, param, 0, 0, channel, song);\n"
                "}\n"
            )
            found = csm.song_state_writes((root,), allowed=csm.SONG_STATE_ALLOWED)
            self.assertEqual(len(found), 1, found)
            self.assertIn(":3:", found[0])

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
