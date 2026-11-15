#include "scenes/bind/runtime_facts.h"

#include <cppspec.hpp>

using namespace deluge::scenes::bind;

// clang-format off
describe bind_tracks("which tracks an audio track may record from", ${
	it("records from any other track that sounds, and never itself, a MIDI track or a CV track", _ {
		constexpr uint32_t kEditing = 4;
		expect(can_record_from(kEditing, {.output_id = 1, .kind = TrackKind::SYNTH})).to_be_true();
		expect(can_record_from(kEditing, {.output_id = 2, .kind = TrackKind::KIT})).to_be_true();
		expect(can_record_from(kEditing, {.output_id = 3, .kind = TrackKind::AUDIO})).to_be_true();
		expect(can_record_from(kEditing, {.output_id = kEditing, .kind = TrackKind::AUDIO})).to_be_false();
		expect(can_record_from(kEditing, {.output_id = 5, .kind = TrackKind::MIDI_OUT})).to_be_false();
		expect(can_record_from(kEditing, {.output_id = 6, .kind = TrackKind::CV})).to_be_false();
	});
});

CPPSPEC_SPEC(bind_tracks)
