// The knob records: the song's own knob mode and a MIDI track's mod-knob CC assignments are the model's, read from a
// song and saved from the record, and every change to them is an Edit that takes no undo step.

#include "model/edits/move_mod_knob_cc.h"
#include "model/edits/set_mod_knob_cc.h"
#include "model/edits/set_mod_knob_mode.h"
#include "model/edits/setting_edit.h"
#include "model/fx/chain_params.h"
#include "model/project_bridge/lane_target.h"

#include "board_controls.hpp"
#include "instruments/classic/pages.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/clip_instance.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/edit.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/project_bridge/controls_bridge.h"
#include "model/project_bridge/song_serialization.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/midi/midi_param_collection.h"
#include "processing/sound/sound.h"
#include "project/structure_codec.h"
#include "storage/storage_manager.h"
#include "ui/controls/knob_focus.h"

#include "integration/scenarios/drivers/driver.h"
#include "integration/scenarios/drivers/tree_load.h"
#include "support/printers/controller_mapping_printer.h"
#include "support/printers/mod_knob_cc_printer.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"
#include "support/matchers/value_matchers.h"
#include "support/song_load.h"

#include "support/spec_main.h"

#include <string>
#include <string_view>
#include <vector>

namespace {

/// @brief A song with one synth track and no clips, saying @p attributes on its song element.
std::string song_xml(std::string_view attributes) {
	return std::string{R"(<?xml version="1.0" encoding="UTF-8"?>
<song firmwareVersion="c1.3.0" earliestCompatibleFirmware="4.1.0-alpha" timePerTimerTick="229" timerTickFraction="-1342177280" inputTickMagnitude="2" )"}
	       + std::string{attributes} + R"(>
	<instruments>
	</instruments>
	<sessionClips>
	</sessionClips>
</song>
)";
}

/// @brief A song with one MIDI track on channel 3, whose element holds @p inner.
std::string midi_song_xml(std::string_view inner) {
	return std::string{R"(<?xml version="1.0" encoding="UTF-8"?>
<song firmwareVersion="c1.3.0" earliestCompatibleFirmware="4.1.0-alpha" timePerTimerTick="229" timerTickFraction="-1342177280" inputTickMagnitude="2">
	<instruments>
		<midiChannel channel="2" suffix="-1">
)"} + std::string{inner}
	       + R"(
		</midiChannel>
	</instruments>
	<sessionClips>
	</sessionClips>
</song>
)";
}

/// @brief The first MIDI track of @p song.
MIDIInstrument* midi_of(Song& song) {
	for (Output* output = song.live.media.first_output; output != nullptr; output = output->next) {
		if (output->type == OutputType::MIDI_OUT) {
			return static_cast<MIDIInstrument*>(output);
		}
	}
	return nullptr;
}

/// @brief @p song as it saves, as text.
std::string saved(Song& song) {
	std::vector<char> buffer(1u << 20);
	const long length = deluge::scenarios::driver::saveSongCapture(song, buffer.data(), buffer.size());
	return length > 0 ? std::string(buffer.data(), static_cast<size_t>(length)) : std::string{};
}

/// @brief The song's own knobs, as the knob focus names them.
deluge::ui::controls::KnobFocus songFocus() {
	return deluge::ui::controls::KnobFocus{.owner = deluge::ui::controls::SongFocus{}};
}

} // namespace

using deluge::edits::KnobModeOwner;
using deluge::project::ModKnobCc;

// clang-format off
describe knob_records("the knob records, by Edit", ${
	it("a song read without activeModFunction holds mode 1, and one read with 4 holds 4", _ {
		Song* const before = currentSong;
		Song* plain = deluge::ui::scenes::spec::load_song(song_xml(""));
		expect(plain).not_().to_be_null();
		if (plain != nullptr) {
			expect(plain->project.mod_knob_mode).to_equal(1);
		}
		Song* four = deluge::ui::scenes::spec::load_song(song_xml(R"(activeModFunction="4")"));
		expect(four).not_().to_be_null();
		if (four != nullptr) {
			expect(four->project.mod_knob_mode).to_equal(4);
			expect(deluge::ui::controls::knob_mode(four->project, songFocus(), kDelugeControls)).to_equal(4);
		}
		currentSong = before;
	});

	it("a song that says a mode past the last button keeps it in its record, and the knobs use the last", _ {
		Song* const before = currentSong;
		Song* nine = deluge::ui::scenes::spec::load_song(song_xml(R"(activeModFunction="9")"));
		expect(nine).not_().to_be_null();
		if (nine != nullptr) {
			expect(nine->project.mod_knob_mode).to_equal(9);
			expect(deluge::ui::controls::knob_mode(nine->project, songFocus(), kDelugeControls)).to_equal(kNumModButtons - 1);
		}
		currentSong = before;
	});

	it("SetModKnobMode on the song writes the record the knobs read, and takes no undo step", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		actionLogger.deleteAllLogs();
		expect(deluge::edits::modKnobModeFor(KnobModeOwner{deluge::project::SongAddress{}}, 5)).to_be_true();
		expect(f.song->project.mod_knob_mode).to_equal(uint8_t{5});
		expect(deluge::ui::controls::knob_mode(f.song->project, songFocus(), kDelugeControls)).to_equal(uint8_t{5});
		expect(actionLogger.firstAction[BEFORE]).to_be_null();
		expect(deluge::edits::modKnobModeFor(KnobModeOwner{deluge::project::SongAddress{}}, kNumModButtons))
		    .to_be_false();
		currentSong = before;
	});

	it("a mode change through any output's knob-mode slot, current or not, reaches its record", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		// A second track in the song that is not the current clip's: an arranger row's held audition targets it.
		Output* held = StorageManager::createNewNonAudioInstrument(OutputType::MIDI_OUT, 6, -1);
		expect(held).not_().to_be_null();
		if (held == nullptr) {
			return;
		}
		f.song->addOutput(held);
		expect(held->docOutput_).not_().to_be_null();
		expect(held->id().has_value()).to_be_true();
		expect(deluge::edits::modKnobModeFor(deluge::project::TrackAddress{*held->id()}, 4)).to_be_true();
		expect(held->modKnobMode).to_equal(uint8_t{4});
		const deluge::project::DocOutput* record = f.song->project.output_structure.find(*held->id());
		expect(record).not_().to_be_null();
		if (record != nullptr) {
			expect(record->mod_knob_mode).to_equal(4);
		}
		expect(deluge::edits::modKnobModeFor(deluge::project::SongAddress{}, 2)).to_be_true();
		expect(f.song->project.mod_knob_mode).to_equal(uint8_t{2});
		// A track the song does not hold takes no mode.
		expect(deluge::edits::modKnobModeFor(deluge::project::TrackAddress{0xFFFFFF}, 3)).to_be_false();
		currentSong = before;
	});

	it("SetModKnobMode on a track writes its record and its output's field", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		const deluge::project::TrackAddress track{*f.instrument->id()};
		expect(deluge::edits::modKnobModeFor(KnobModeOwner{track}, 3)).to_be_true();
		expect(f.instrument->modKnobMode).to_equal(uint8_t{3});
		const deluge::project::DocOutput* record = f.song->project.output_structure.find(*f.instrument->id());
		expect(record).not_().to_be_null();
		if (record != nullptr) {
			expect(record->mod_knob_mode).to_equal(3);
		}
		currentSong = before;
	});

	it("a song saved after its mode changes to 6 says activeModFunction 6", _ {
		Song* const before = currentSong;
		Song* song = deluge::ui::scenes::spec::load_song(song_xml(""));
		expect(song).not_().to_be_null();
		if (song != nullptr) {
			currentSong = song;
			expect(deluge::edits::modKnobModeFor(KnobModeOwner{deluge::project::SongAddress{}}, 6)).to_be_true();
			const std::string text = saved(*song);
			// A song saves as JSON; XML says the same attribute.
			expect(text).to_satisfy(
			    [](const std::string& t) {
				    return t.contains(R"("activeModFunction": 6)") || t.contains(R"(activeModFunction="6")");
			    },
			    "activeModFunction 6, as JSON or XML");
		}
		currentSong = before;
	});

	it("a MIDI track's modKnobs load into its record, each word as the file says it", _ {
		Song* const before = currentSong;
		Song* song = deluge::ui::scenes::spec::load_song(midi_song_xml(
		    R"(<modKnobs><modKnob cc="none" /><modKnob cc="bend" /><modKnob cc="aftertouch" /><modKnob cc="74" /></modKnobs>)"));
		MIDIInstrument* midi = song != nullptr ? midi_of(*song) : nullptr;
		expect(midi).not_().to_be_null();
		if (midi != nullptr) {
			expect(midi->docOutput_).not_().to_be_null();
		}
		if (midi != nullptr && midi->docOutput_ != nullptr) {
			const std::vector<ModKnobCc>& ccs = midi->docOutput_->mod_knob_ccs;
			to_be_at_least(expect(ccs.size()), size_t{4});
			if (ccs.size() >= 4) {
				expect(ccs[0]).to_equal(ModKnobCc{});
				expect(ccs[1]).to_equal(ModKnobCc{.kind = ModKnobCc::Kind::BEND});
				expect(ccs[2]).to_equal(ModKnobCc{.kind = ModKnobCc::Kind::AFTERTOUCH});
				expect(ccs[3]).to_equal(ModKnobCc{.kind = ModKnobCc::Kind::NUMBER, .number = 74});
			}
		}
		currentSong = before;
	});

	it("a knob on CC 1 is the Y axis, read the same by the engine and the model", _ {
		Song* const before = currentSong;
		Song* song = deluge::ui::scenes::spec::load_song(midi_song_xml(R"(<modKnobs><modKnob cc="1" /></modKnobs>)"));
		MIDIInstrument* midi = song != nullptr ? midi_of(*song) : nullptr;
		const ModKnobCc y_axis{.kind = ModKnobCc::Kind::NUMBER, .number = CC_NUMBER_Y_AXIS};
		expect(midi).not_().to_be_null();
		if (midi != nullptr) {
			expect(midi->docOutput_).not_().to_be_null();
			if (midi->docOutput_ != nullptr) {
				expect(midi->docOutput_->mod_knob_ccs.empty()).to_be_false();
				if (!midi->docOutput_->mod_knob_ccs.empty()) {
					expect(midi->docOutput_->mod_knob_ccs[0]).to_equal(y_axis);
				}
			}
		}

		deluge::serialization::Value knob = deluge::serialization::Value::makeMap();
		knob.set("cc", deluge::serialization::Value::makeNumber(1));
		deluge::serialization::Value box = deluge::serialization::Value::makeMap();
		box.set("modKnob", std::move(knob));
		deluge::serialization::Value knobs = deluge::serialization::Value::makeArray();
		knobs.append(std::move(box));
		deluge::serialization::Value element = deluge::serialization::Value::makeMap();
		element.set("modKnobs", std::move(knobs));
		deluge::project::DocOutput decoded = deluge::project::new_output("midiChannel");
		expect(deluge::project::decode_identity(element, "midiChannel", decoded).has_value()).to_be_true();
		expect(decoded.mod_knob_ccs.empty()).to_be_false();
		if (!decoded.mod_knob_ccs.empty()) {
			expect(decoded.mod_knob_ccs[0]).to_equal(y_axis);
		}
		currentSong = before;
	});

	it("an unedited MIDI track saves no modKnobs", _ {
		Song* const before = currentSong;
		Song* song = deluge::ui::scenes::spec::load_song(midi_song_xml(""));
		expect(song).not_().to_be_null();
		if (song != nullptr) {
			currentSong = song;
			expect(saved(*song)).not_().to_contain("modKnobs");
		}
		currentSong = before;
	});

	it("a MIDI track edited through a setting records its knobs as it then saves them", _ {
		Song* const before = currentSong;
		Song* song = deluge::ui::scenes::spec::load_song(midi_song_xml(""));
		MIDIInstrument* midi = song != nullptr ? midi_of(*song) : nullptr;
		expect(midi).not_().to_be_null();
		if (midi != nullptr) {
			expect(midi->docOutput_).not_().to_be_null();
			if (midi->docOutput_ != nullptr) {
				expect(midi->editedByUser).to_be_false();
			}
		}
		if (midi != nullptr && midi->docOutput_ != nullptr) {
			currentSong = song;
			expect(midi->docOutput_->mod_knob_ccs.empty()).to_be_true();
			deluge::edits::trackSettingFor(*midi, "collapseMpe", deluge::serialization::Value::makeNumber(0));
			expect(midi->editedByUser).to_be_true();
			expect(midi->docOutput_->mod_knob_ccs.size()).to_equal(midi->modKnobCCAssignments.size());
		}
		currentSong = before;
	});

	it("SetModKnobCC writes the record and the engine, marks the track edited, and takes no undo step", _ {
		Song* const before = currentSong;
		Song* song = deluge::ui::scenes::spec::load_song(midi_song_xml(""));
		MIDIInstrument* midi = song != nullptr ? midi_of(*song) : nullptr;
		expect(midi).not_().to_be_null();
		if (midi != nullptr) {
			expect(midi->id().has_value()).to_be_true();
		}
		if (midi != nullptr && midi->id()) {
			currentSong = song;
			actionLogger.deleteAllLogs();
			const ModKnobCc twenty{.kind = ModKnobCc::Kind::NUMBER, .number = 20};
			expect(deluge::edits::modKnobCCFor(deluge::project::TrackAddress{*midi->id()}, 5, twenty)).to_be_true();
			expect(midi->docOutput_->mod_knob_ccs.size()).to_be_greater_than(size_t{5});
			if (midi->docOutput_->mod_knob_ccs.size() > size_t{5}) {
				expect(midi->docOutput_->mod_knob_ccs[5]).to_equal(twenty);
			}
			expect(static_cast<int32_t>(midi->modKnobCCAssignments[5])).to_equal(20);
			expect(midi->editedByUser).to_be_true();
			expect(actionLogger.firstAction[BEFORE]).to_be_null();
			expect(saved(*song)).to_contain("modKnobs");
		}
		currentSong = before;
	});

	it("the gold knob's CC change goes through the record", _ {
		Song* const before = currentSong;
		Song* song = deluge::ui::scenes::spec::load_song(midi_song_xml(R"(<modKnobs><modKnob cc="74" /></modKnobs>)"));
		MIDIInstrument* midi = song != nullptr ? midi_of(*song) : nullptr;
		expect(midi).not_().to_be_null();
		if (midi != nullptr) {
			expect(midi->docOutput_).not_().to_be_null();
		}
		if (midi != nullptr && midi->docOutput_ != nullptr) {
			currentSong = song;
			const int32_t next = midi->changeControlNumberForModKnob(1, 0, 0);
			expect(next).not_().to_equal(74);
			expect(midi->docOutput_->mod_knob_ccs[0])
			    .to_equal(ModKnobCc{.kind = ModKnobCc::Kind::NUMBER, .number = static_cast<uint8_t>(next)});
		}
		currentSong = before;
	});

	it("a MIDI track's assignments copied into a new MIDI instrument are its record once the song adopts it", _ {
		Song* const before = currentSong;
		Song* song = deluge::ui::scenes::spec::load_song(midi_song_xml(R"(<modKnobs><modKnob cc="none" /><modKnob cc="12" /></modKnobs>)"));
		MIDIInstrument* old = song != nullptr ? midi_of(*song) : nullptr;
		expect(old).not_().to_be_null();
		if (old != nullptr) {
			currentSong = song;
			auto* made = static_cast<MIDIInstrument*>(
			    StorageManager::createNewNonAudioInstrument(OutputType::MIDI_OUT, 5, -1));
			expect(made).not_().to_be_null();
			if (made != nullptr) {
				made->copyModKnobCCsFrom(*old);
				song->addOutput(made);
				expect(made->docOutput_).not_().to_be_null();
				if (made->docOutput_ != nullptr) {
					expect(made->docOutput_->mod_knob_ccs.size()).to_be_greater_than(size_t{1});
				}
				if (made->docOutput_ != nullptr && made->docOutput_->mod_knob_ccs.size() > 1) {
					expect(made->docOutput_->mod_knob_ccs[1])
					    .to_equal(ModKnobCc{.kind = ModKnobCc::Kind::NUMBER, .number = 12});
				}
			}
		}
		currentSong = before;
	});

	it("moving a knob that sends nothing leaves it so, and reports it, as legacy's move does", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_midi_song();
		Song* const before = currentSong;
		currentSong = f.song;
		auto* midi = static_cast<MIDIInstrument*>(f.instrument);
		const std::optional<int32_t> moved =
		    deluge::edits::moveModKnobCCFor(deluge::project::TrackAddress{*midi->id()}, 0, 1);
		expect(moved).to_equal(std::optional<int32_t>{CC_NUMBER_NONE});
		expect(static_cast<int32_t>(midi->modKnobCCAssignments[0])).to_equal(static_cast<int32_t>(CC_NUMBER_NONE));
		currentSong = before;
	});

	it("moving a knob's CC leaves a clip that never had the old CC as it is", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_midi_song();
		Song* const before = currentSong;
		currentSong = f.song;
		auto* midi = static_cast<MIDIInstrument*>(f.instrument);
		InstrumentClip* second = deluge::ui::scenes::spec::detail::make_session_clip(f.song);
		{
			ParamManagerForTimeline params;
			params.setupMIDI();
			second->setAudioInstrument(midi, f.song, true, &params);
		}
		expect(deluge::edits::modKnobCCFor(deluge::project::TrackAddress{*midi->id()}, 0,
		                                   ModKnobCc{.kind = ModKnobCc::Kind::NUMBER, .number = 74}))
		    .to_be_true();
		// The first clip automates CC 74; the second has none of it, and holds a plain value on CC 75.
		{
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* param = midi->getParamToControlFromInputMIDIChannel(
			    74, setupModelStackWithThreeMainThingsButNoNoteRow(memory, f.song, midi, f.clip, &f.clip->paramManager));
			param->autoParam->setValuePossiblyForRegion(1 << 28, deluge::bridge::target_of(*param, std::nullopt), 0, f.clip->loopLength() / 2);
		}
		MIDIParamCollection* second_ccs = second->paramManager.getMIDIParamCollection();
		MIDIParam* plain = second_ccs->params.getOrCreateParamFromCC(75, 1234);
		expect(plain).not_().to_be_null();
		expect(second_ccs->params.getParamFromCC(74)).to_be_null();

		const std::optional<int32_t> moved =
		    deluge::edits::moveModKnobCCFor(deluge::project::TrackAddress{*midi->id()}, 0, 1);
		expect(moved).to_equal(std::optional<int32_t>{75});
		MIDIParam* after = second_ccs->params.getParamFromCC(75);
		expect(after).not_().to_be_null();
		if (after != nullptr) {
			expect(after->param.getCurrentValue()).to_equal(1234);
		}
		expect(second_ccs->params.getParamFromCC(74)).to_be_null();
		currentSong = before;
	});

	it("moving a knob's CC carries its automation in every clip, the arrangement's own too, takes no undo step, and the record names the new CC", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_midi_song();
		Song* const before = currentSong;
		currentSong = f.song;
		auto* midi = static_cast<MIDIInstrument*>(f.instrument);
		InstrumentClip* second = deluge::ui::scenes::spec::detail::make_session_clip(f.song);
		{
			ParamManagerForTimeline params;
			params.setupMIDI();
			second->setAudioInstrument(midi, f.song, true, &params);
		}
		expect(deluge::edits::modKnobCCFor(deluge::project::TrackAddress{*midi->id()}, 0,
		                                   ModKnobCc{.kind = ModKnobCc::Kind::NUMBER, .number = 74}))
		    .to_be_true();
		// Automation on CC 74 in each clip: a held-pad region over the whole clip.
		// And an arrangement-only clip, which the arrangement alone plays.
		InstrumentClip* arranged = deluge::ui::scenes::spec::detail::place<InstrumentClip>(*f.song);
		arranged->setLoopLength(f.clip->loopLength());
		(void)f.song->live.clips.arrangement_only_clips.insertClipAt(arranged, 0);
		{
			ParamManagerForTimeline params;
			params.setupMIDI();
			arranged->setAudioInstrument(midi, f.song, true, &params);
		}
		arranged->setSection(255);
		if (const int32_t at = midi->clipInstances.insertSorted(0).value_or(-1); at >= 0) {
			midi->clipInstances[at].clip = arranged;
			midi->clipInstances[at].length = arranged->loopLength();
		}
		const auto stackOf = [&](InstrumentClip* clip, char* memory) {
			return setupModelStackWithThreeMainThingsButNoNoteRow(memory, f.song, midi, clip, &clip->paramManager);
		};
		const auto automated = [&](InstrumentClip* clip, int32_t cc) {
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* param = midi->getParamToControlFromInputMIDIChannel(cc, stackOf(clip, memory));
			return param != nullptr && param->autoParam != nullptr && param->autoParam->isAutomated(*param->doc());
		};
		for (InstrumentClip* clip : {f.clip, second, arranged}) {
			alignas(ModelStackWithAutoParam) char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* param = midi->getParamToControlFromInputMIDIChannel(74, stackOf(clip, memory));
			expect(param).not_().to_be_null();
			if (param != nullptr) {
				expect(param->autoParam).not_().to_be_null();
			}
			if (param != nullptr && param->autoParam != nullptr) {
				param->autoParam->setValuePossiblyForRegion(1 << 28, deluge::bridge::target_of(*param, std::nullopt), 0, clip->loopLength() / 2);
			}
			expect(automated(clip, 74)).to_be_true();
		}

		actionLogger.deleteAllLogs();
		const std::optional<int32_t> moved =
		    deluge::edits::moveModKnobCCFor(deluge::project::TrackAddress{*midi->id()}, 0, 1);
		expect(moved.has_value()).to_be_true();
		if (moved.has_value()) {
			expect(*moved).not_().to_equal(74);
		}
		expect(actionLogger.firstAction[BEFORE]).to_be_null();
		if (moved) {
			for (InstrumentClip* clip : {f.clip, second, arranged}) {
				expect(automated(clip, *moved)).to_be_true();
				expect(automated(clip, 74)).to_be_false();
			}
			expect(midi->docOutput_->mod_knob_ccs[0])
			    .to_equal(ModKnobCc{.kind = ModKnobCc::Kind::NUMBER, .number = static_cast<uint8_t>(*moved)});
			expect(static_cast<int32_t>(midi->modKnobCCAssignments[0])).to_equal(*moved);
		}
		currentSong = before;
	});

	it("a freshly made synth's knobs are the instrument's default pages", _ {
		deluge::ui::scenes::spec::SongFixture f = deluge::ui::scenes::spec::make_synth_song();
		std::vector<deluge::project::ControllerMapping> expected;
		for (const deluge::plugin::RemotePage& page : deluge::instruments::classic::kDevicePages) {
			for (uint8_t knob = 0; knob < 2; knob++) {
				if (const auto& param = page.slots[knob].param) {
					expected.push_back({deluge::project::ModKnobControl{static_cast<uint8_t>(page.section), knob},
				                    deluge::project::engine_target(param->value)});
				}
			}
		}
		const std::vector<deluge::project::ControllerMapping> made =
		    deluge::bridge::controlsOf(*static_cast<Sound*>(static_cast<SoundInstrument*>(f.instrument)));
		expect(made).to_equal(expected);
	});
});
// clang-format on

CPPSPEC_SPEC(knob_records)
