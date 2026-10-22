// The knob records: the song's own knob mode and a MIDI track's mod-knob CC assignments are the model's, read from a
// song and saved from the record, and every change to them is an Edit that takes no undo step.

#include "model/edits/set_mod_knob_mode.h"

#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/edits/edit.h"
#include "model/global_effectable/global_effectable.h"
#include "model/project_bridge/song_serialization.h"
#include "model/song/song.h"
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
});
// clang-format on

CPPSPEC_SPEC(knob_records)
