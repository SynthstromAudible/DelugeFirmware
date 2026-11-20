#include "scenes/bind/ports/track_facts.h"

#include <cppspec.hpp>

#include <string_view>

using namespace deluge::scenes::bind;

// clang-format off
describe bind_tracks("the song's track facts", ${
	it("records from any other track that sounds, and never itself, a MIDI track or a CV track", _ {
		constexpr uint32_t kEditing = 4;
		expect(can_record_from(kEditing, {.output_id = 1, .kind = TrackKind::SYNTH})).to_be_true();
		expect(can_record_from(kEditing, {.output_id = 2, .kind = TrackKind::KIT})).to_be_true();
		expect(can_record_from(kEditing, {.output_id = 3, .kind = TrackKind::AUDIO})).to_be_true();
		expect(can_record_from(kEditing, {.output_id = kEditing, .kind = TrackKind::AUDIO})).to_be_false();
		expect(can_record_from(kEditing, {.output_id = 5, .kind = TrackKind::MIDI_OUT})).to_be_false();
		expect(can_record_from(kEditing, {.output_id = 6, .kind = TrackKind::CV})).to_be_false();
	});
	it("names each track kind as the menus show it, a MIDI track by its channel and an audio track by its mode", _ {
		using sv = std::string_view;
		expect(track_kind_name(TrackKind::SYNTH, 0)).to_equal(sv{"Synth"});
		expect(track_kind_name(TrackKind::SYNTH, 5)).to_equal(sv{"Synth"});
		expect(track_kind_name(TrackKind::KIT, 0)).to_equal(sv{"Kit"});
		expect(track_kind_name(TrackKind::KIT, 5)).to_equal(sv{"Kit"});
		expect(track_kind_name(TrackKind::CV, 0)).to_equal(sv{"CV / gate"});
		expect(track_kind_name(TrackKind::CV, 5)).to_equal(sv{"CV / gate"});
		expect(track_kind_name(TrackKind::MIDI_OUT, -1)).to_equal(sv{"MIDI"});
		expect(track_kind_name(TrackKind::MIDI_OUT, 0)).to_equal(sv{"MIDI"});
		expect(track_kind_name(TrackKind::MIDI_OUT, 5)).to_equal(sv{"MIDI"});
		expect(track_kind_name(TrackKind::MIDI_OUT, 15)).to_equal(sv{"MIDI"});
		expect(track_kind_name(TrackKind::MIDI_OUT, 16)).to_equal(sv{"MPE"});
		expect(track_kind_name(TrackKind::MIDI_OUT, 17)).to_equal(sv{"MPE"});
		expect(track_kind_name(TrackKind::MIDI_OUT, 18)).to_equal(sv{"Internal"});
		expect(track_kind_name(TrackKind::MIDI_OUT, 55)).to_equal(sv{"Internal"});
		expect(track_kind_name(TrackKind::MIDI_OUT, 255)).to_equal(sv{"Internal"});
		expect(track_kind_name(TrackKind::AUDIO, 0)).to_equal(sv{"Audio Player"});
		expect(track_kind_name(TrackKind::AUDIO, 1)).to_equal(sv{"Audio Sampler"});
		expect(track_kind_name(TrackKind::AUDIO, 2)).to_equal(sv{"Audio Looper"});
		expect(track_kind_name(TrackKind::AUDIO, 3)).to_equal(sv{"Audio"});
		expect(track_kind_name(TrackKind::AUDIO, 5)).to_equal(sv{"Audio"});
		expect(track_kind_name(TrackKind::AUDIO, 256)).to_equal(sv{"Audio Player"});
	});
});

CPPSPEC_SPEC(bind_tracks)
