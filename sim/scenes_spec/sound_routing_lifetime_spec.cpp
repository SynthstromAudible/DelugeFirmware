/// @file
/// @brief A sound's routing through its lifetime: the routing a row holds before a kit adopts it moves into the row's
/// slot and is played from there; when the row's record goes, the row takes it back; the runtime's copies follow.

#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"
#include "project/instrument_slot.h"
#include "support/learned_fixture.h"
#include "support/printers/sound_routing_printer.h"
#include "support/song_fixture.h"

#include "cppspec.hpp"
#include "support/spec_main.h"

#include <cstdint>
#include <optional>

using namespace deluge::ui::scenes;

namespace {

/// A routing nothing new has: a send, a channel and a note.
constexpr deluge::project::SoundRouting kRouted{.side_chain_send = 123456, .midi_channel = 4, .midi_note = 40};

/// A sample row made as a kit-row preset makes it, holding @p routing before any kit adopts it.
SoundDrum* row_holding(spec::SongFixture& f, const deluge::project::SoundRouting& routing) {
	SoundDrum* drum = spec::learned::makeSoundDrum(f);
	drum->routingRecord() = routing;
	drum->applyRouting();
	return drum;
}

} // namespace

// clang-format off
describe sound_routing_lifetime("a sound's routing, from its own to its kit row's and back", ${
	it("moves into the row's slot when the kit adopts the row, and plays from there", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* drum = row_holding(f, kRouted);
		static_cast<Kit*>(f.instrument)->addDrum(drum);
		expect(drum->docDrum_).not_().to_be_null();
		expect(drum->docDrum_->instrument.has_value()).to_be_true();
		expect(drum->docDrum_->instrument->routing).to_equal(kRouted);
		expect(&drum->routing()).to_equal(&drum->docDrum_->instrument->routing);
		expect(drum->sideChainSendLevel).to_equal(int32_t{123456});
		expect(static_cast<int>(drum->outputMidiChannel)).to_equal(4);
		expect(static_cast<int>(drum->outputMidiNoteForDrum)).to_equal(40);
	});

	it("takes its routing back when the row's record goes, still playing it", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* drum = row_holding(f, kRouted);
		static_cast<Kit*>(f.instrument)->addDrum(drum);
		const deluge::project::SoundRouting* slot = &drum->docDrum_->instrument->routing;
		f.song->removeDocDrum(drum);
		expect(&drum->routing()).not_().to_equal(slot);
		expect(drum->routing()).to_equal(kRouted);
		expect(drum->sideChainSendLevel).to_equal(int32_t{123456});
		expect(static_cast<int>(drum->outputMidiChannel)).to_equal(4);
		expect(static_cast<int>(drum->outputMidiNoteForDrum)).to_equal(40);
	});

	it("plays a new row's routing as no send and no MIDI output", _ {
		spec::SongFixture f = spec::make_kit_song();
		SoundDrum* drum = row_holding(f, deluge::project::SoundRouting{});
		static_cast<Kit*>(f.instrument)->addDrum(drum);
		expect(drum->routing()).to_equal(deluge::project::SoundRouting{});
		expect(drum->sideChainSendLevel).to_equal(int32_t{0});
		expect(static_cast<int>(drum->outputMidiChannel)).to_equal(static_cast<int>(MIDI_CHANNEL_NONE));
		expect(static_cast<int>(drum->outputMidiNoteForDrum)).to_equal(static_cast<int>(MIDI_NOTE_NONE));
	});
});
// clang-format on

DELUGE_SPEC(sound_routing_lifetime_spec, sound_routing_lifetime)
