// The knob records: the song's own knob mode and a MIDI track's mod-knob CC assignments are the model's, read from a
// song and saved from the record, and every change to them is an Edit that takes no undo step.

#include "model/edits/move_mod_knob_cc.h"
#include "model/edits/set_mod_knob_cc.h"
#include "model/edits/set_mod_knob_mode.h"

#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/edits/edit.h"
#include "model/global_effectable/global_effectable.h"
#include "model/instrument/midi_instrument.h"
#include "model/model_stack.h"
#include "model/project_bridge/song_serialization.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "storage/storage_manager.h"

#include "integration/scenarios/drivers/driver.h"
#include "integration/scenarios/drivers/tree_load.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"

#include "spec/support/spec_main.h"

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

/// @brief Load @p xml into a new song, as a song file is read; null if the read failed.
Song* load_song(std::string_view xml) {
	void* memory = GeneralMemoryAllocator::get().alloc(sizeof(Song));
	Song* song = new (memory) Song();
	song->paramManager.setupUnpatched();
	GlobalEffectable::initParams(&song->paramManager);
	std::optional<deluge::storage::Value> tree = parseDocument(xml, deluge::storage::DocumentFormat::XML);
	if (!tree) {
		return nullptr;
	}
	deluge::storage::ValueTreeDeserializer adapter(*tree, sourceFormatOf(deluge::storage::DocumentFormat::XML));
	activeReader = &adapter;
	Error error = openTreeFile(adapter, "song", deluge::storage::DocumentFormat::XML);
	if (error == Error::NONE) {
		error = deluge::bridge::loadSong(*song, adapter);
	}
	activeReader = &smDeserializer;
	return error == Error::NONE ? song : nullptr;
}

/// @brief @p song as it saves, as text.
std::string saved(Song& song) {
	std::vector<char> buffer(1u << 20);
	const long length = deluge::scenarios::driver::saveSongCapture(song, buffer.data(), buffer.size());
	return length > 0 ? std::string(buffer.data(), static_cast<size_t>(length)) : std::string{};
}

} // namespace

using deluge::edits::KnobModeOwner;
using deluge::project::ModKnobCc;

// clang-format off
describe knob_records("the knob records, by Edit", ${
	it("a song read without activeModFunction holds mode 1, and one read with 4 holds 4", _ {
		Song* const before = currentSong;
		Song* plain = load_song(song_xml(""));
		expect(plain != nullptr && plain->project.mod_knob_mode == 1).to_be_true();
		Song* four = load_song(song_xml(R"(activeModFunction="4")"));
		expect(four != nullptr && four->project.mod_knob_mode == 4).to_be_true();
		expect(four != nullptr && four->live.global_effectable.modKnobMode == 4).to_be_true();
		currentSong = before;
	});

	it("a song that says a mode past the last button keeps it in its record, and the engine uses the last", _ {
		Song* const before = currentSong;
		Song* nine = load_song(song_xml(R"(activeModFunction="9")"));
		expect(nine != nullptr && nine->project.mod_knob_mode == 9).to_be_true();
		expect(nine != nullptr && nine->live.global_effectable.modKnobMode == kNumModButtons - 1).to_be_true();
		currentSong = before;
	});

	it("SetModKnobMode on the song writes the record and the engine, and takes no undo step", _ {
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		actionLogger.deleteAllLogs();
		expect(deluge::edits::modKnobModeFor(KnobModeOwner{deluge::project::SongAddress{}}, 5)).to_be_true();
		expect(f.song->project.mod_knob_mode).to_equal(uint8_t{5});
		expect(f.song->live.global_effectable.modKnobMode).to_equal(uint8_t{5});
		expect(actionLogger.firstAction[BEFORE] == nullptr).to_be_true();
		expect(deluge::edits::modKnobModeFor(KnobModeOwner{deluge::project::SongAddress{}}, kNumModButtons))
		    .to_be_false();
		currentSong = before;
	});

	it("SetModKnobMode on a track writes its record and its output's field", _ {
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_synth_song();
		Song* const before = currentSong;
		currentSong = f.song;
		const deluge::project::TrackAddress track{*f.instrument->id()};
		expect(deluge::edits::modKnobModeFor(KnobModeOwner{track}, 3)).to_be_true();
		expect(f.instrument->modKnobMode).to_equal(uint8_t{3});
		const deluge::project::DocOutput* record = f.song->project.output_structure.find(*f.instrument->id());
		expect(record != nullptr && record->mod_knob_mode == 3).to_be_true();
		currentSong = before;
	});

	it("a song saved after its mode changes to 6 says activeModFunction 6", _ {
		Song* const before = currentSong;
		Song* song = load_song(song_xml(""));
		expect(song != nullptr).to_be_true();
		if (song != nullptr) {
			currentSong = song;
			expect(deluge::edits::modKnobModeFor(KnobModeOwner{deluge::project::SongAddress{}}, 6)).to_be_true();
			const std::string text = saved(*song);
			// A song saves as JSON; XML says the same attribute.
			expect(text.find(R"("activeModFunction": 6)") != std::string::npos
			       || text.find(R"(activeModFunction="6")") != std::string::npos)
			    .to_be_true();
		}
		currentSong = before;
	});

	it("a MIDI track's modKnobs load into its record, each word as the file says it", _ {
		Song* const before = currentSong;
		Song* song = load_song(midi_song_xml(
		    R"(<modKnobs><modKnob cc="none" /><modKnob cc="bend" /><modKnob cc="aftertouch" /><modKnob cc="74" /></modKnobs>)"));
		MIDIInstrument* midi = song != nullptr ? midi_of(*song) : nullptr;
		expect(midi != nullptr && midi->docOutput_ != nullptr).to_be_true();
		if (midi != nullptr && midi->docOutput_ != nullptr) {
			const std::vector<ModKnobCc>& ccs = midi->docOutput_->mod_knob_ccs;
			expect(ccs.size() >= size_t{4}).to_be_true();
			if (ccs.size() >= 4) {
				expect(ccs[0] == ModKnobCc{}).to_be_true();
				expect(ccs[1] == ModKnobCc{.kind = ModKnobCc::Kind::BEND}).to_be_true();
				expect(ccs[2] == ModKnobCc{.kind = ModKnobCc::Kind::AFTERTOUCH}).to_be_true();
				expect(ccs[3] == (ModKnobCc{.kind = ModKnobCc::Kind::NUMBER, .number = 74})).to_be_true();
			}
		}
		currentSong = before;
	});

	it("an unedited MIDI track saves no modKnobs", _ {
		Song* const before = currentSong;
		Song* song = load_song(midi_song_xml(""));
		expect(song != nullptr).to_be_true();
		if (song != nullptr) {
			currentSong = song;
			expect(saved(*song).find("modKnobs") == std::string::npos).to_be_true();
		}
		currentSong = before;
	});

	it("SetModKnobCC writes the record and the engine, marks the track edited, and takes no undo step", _ {
		Song* const before = currentSong;
		Song* song = load_song(midi_song_xml(""));
		MIDIInstrument* midi = song != nullptr ? midi_of(*song) : nullptr;
		expect(midi != nullptr && midi->id().has_value()).to_be_true();
		if (midi != nullptr && midi->id()) {
			currentSong = song;
			actionLogger.deleteAllLogs();
			const ModKnobCc twenty{.kind = ModKnobCc::Kind::NUMBER, .number = 20};
			expect(deluge::edits::modKnobCCFor(deluge::project::TrackAddress{*midi->id()}, 5, twenty)).to_be_true();
			expect(midi->docOutput_->mod_knob_ccs.size() > size_t{5} && midi->docOutput_->mod_knob_ccs[5] == twenty)
			    .to_be_true();
			expect(static_cast<int32_t>(midi->modKnobCCAssignments[5])).to_equal(20);
			expect(midi->editedByUser).to_be_true();
			expect(actionLogger.firstAction[BEFORE] == nullptr).to_be_true();
			expect(saved(*song).find("modKnobs") != std::string::npos).to_be_true();
		}
		currentSong = before;
	});

	it("the gold knob's CC change goes through the record", _ {
		Song* const before = currentSong;
		Song* song = load_song(midi_song_xml(R"(<modKnobs><modKnob cc="74" /></modKnobs>)"));
		MIDIInstrument* midi = song != nullptr ? midi_of(*song) : nullptr;
		expect(midi != nullptr && midi->docOutput_ != nullptr).to_be_true();
		if (midi != nullptr && midi->docOutput_ != nullptr) {
			currentSong = song;
			const int32_t next = midi->changeControlNumberForModKnob(1, 0, 0);
			expect(next != 74).to_be_true();
			expect(midi->docOutput_->mod_knob_ccs[0] == (ModKnobCc{.kind = ModKnobCc::Kind::NUMBER,
			                                                        .number = static_cast<uint8_t>(next)}))
			    .to_be_true();
		}
		currentSong = before;
	});

	it("a MIDI track's assignments copied into a new MIDI instrument are its record once the song adopts it", _ {
		Song* const before = currentSong;
		Song* song = load_song(midi_song_xml(R"(<modKnobs><modKnob cc="none" /><modKnob cc="12" /></modKnobs>)"));
		MIDIInstrument* old = song != nullptr ? midi_of(*song) : nullptr;
		expect(old != nullptr).to_be_true();
		if (old != nullptr) {
			currentSong = song;
			auto* made = static_cast<MIDIInstrument*>(
			    StorageManager::createNewNonAudioInstrument(OutputType::MIDI_OUT, 5, -1));
			expect(made != nullptr).to_be_true();
			if (made != nullptr) {
				made->copyModKnobCCsFrom(*old);
				song->addOutput(made);
				expect(made->docOutput_ != nullptr && made->docOutput_->mod_knob_ccs.size() > size_t{1}).to_be_true();
				if (made->docOutput_ != nullptr && made->docOutput_->mod_knob_ccs.size() > 1) {
					expect(made->docOutput_->mod_knob_ccs[1] == (ModKnobCc{.kind = ModKnobCc::Kind::NUMBER, .number = 12}))
					    .to_be_true();
				}
			}
		}
		currentSong = before;
	});

	it("moving a knob's CC carries its automation in both clips, and the record names the new CC", _ {
		deluge::scenes::spec::SongFixture f = deluge::scenes::spec::make_midi_song();
		Song* const before = currentSong;
		currentSong = f.song;
		auto* midi = static_cast<MIDIInstrument*>(f.instrument);
		InstrumentClip* second = deluge::scenes::spec::detail::make_session_clip(f.song);
		{
			ParamManagerForTimeline params;
			params.setupMIDI();
			second->setAudioInstrument(midi, f.song, true, &params);
		}
		expect(deluge::edits::modKnobCCFor(deluge::project::TrackAddress{*midi->id()}, 0,
		                                   ModKnobCc{.kind = ModKnobCc::Kind::NUMBER, .number = 74}))
		    .to_be_true();
		// Automation on CC 74 in each clip: a held-pad region over the whole clip.
		const auto stackOf = [&](InstrumentClip* clip, char* memory) {
			return setupModelStackWithThreeMainThingsButNoNoteRow(memory, f.song, midi, clip, &clip->paramManager);
		};
		const auto automated = [&](InstrumentClip* clip, int32_t cc) {
			char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* param = midi->getParamToControlFromInputMIDIChannel(cc, stackOf(clip, memory));
			return param != nullptr && param->autoParam != nullptr && param->autoParam->isAutomated(*param->doc());
		};
		for (InstrumentClip* clip : {f.clip, second}) {
			char memory[MODEL_STACK_MAX_SIZE];
			ModelStackWithAutoParam* param = midi->getParamToControlFromInputMIDIChannel(74, stackOf(clip, memory));
			expect(param != nullptr && param->autoParam != nullptr).to_be_true();
			if (param != nullptr && param->autoParam != nullptr) {
				param->autoParam->setValuePossiblyForRegion(1 << 28, param, 0, clip->loopLength() / 2);
			}
			expect(automated(clip, 74)).to_be_true();
		}

		const std::optional<int32_t> moved =
		    deluge::edits::moveModKnobCCFor(deluge::project::TrackAddress{*midi->id()}, 0, 1);
		expect(moved.has_value() && *moved != 74).to_be_true();
		if (moved) {
			for (InstrumentClip* clip : {f.clip, second}) {
				expect(automated(clip, *moved)).to_be_true();
				expect(automated(clip, 74)).to_be_false();
			}
			expect(midi->docOutput_->mod_knob_ccs[0]
			       == (ModKnobCc{.kind = ModKnobCc::Kind::NUMBER, .number = static_cast<uint8_t>(*moved)}))
			    .to_be_true();
			expect(static_cast<int32_t>(midi->modKnobCCAssignments[0])).to_equal(*moved);
		}
		currentSong = before;
	});
});
// clang-format on

CPPSPEC_SPEC(knob_records)
