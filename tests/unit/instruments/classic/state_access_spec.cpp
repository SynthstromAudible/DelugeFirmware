#include "instruments/classic/state_access.h"
#include "support/printers/sound_record_printer.h"
#include "support/record_state.h"

#include <cppspec.hpp>
#include <string>
#include <utility>

using namespace deluge::instruments::classic;

namespace {

/// A subtractive sound whose first oscillator is tuned up and whose second plays one looped, reversed sample.
SoundState sample_on_b() {
	SoundState s;
	deluge::test::set(s, "osc1/transpose", "7");
	deluge::test::set(s, "osc1/cents", "-3");
	s = deluge::test::with_kind(s, SourceId::B, OscType::SAMPLE);
	deluge::test::set(s, "osc2/loopMode", "2"); // LOOP
	deluge::test::set(s, "osc2/reversed", "1");
	return with_ranges(std::move(s), SourceId::B,
	                   {{.top_note = 60, .file_path = "SAMPLES/A.WAV", .zone = {.end = 100}}});
}

/// Whether slot @p id reads the same in @p a and @p b: its type, tuning, playback and ranges.
bool same_slot(const SoundState& a, const SoundState& b, SourceId id) {
	return source_type(a, id) == source_type(b, id) && source_tuning(a, id) == source_tuning(b, id)
	       && sample_playback(a, id) == sample_playback(b, id) && same_ranges(a, b, id);
}

} // namespace

// clang-format off
describe state_access("the Deluge instrument's state, as code outside the instrument reads it", ${
	it("reads each slot's type, tuning and playback", _ {
		const SoundState s = sample_on_b();
		expect(source_type(s, SourceId::A)).to_equal(OscType::SQUARE);
		expect(source_type(s, SourceId::B)).to_equal(OscType::SAMPLE);
		expect(source_tuning(s, SourceId::A)).to_equal(Tuning{.transpose = 7, .cents = -3});
		expect(sample_playback(s, SourceId::B).repeat_mode).to_equal(SampleRepeatMode::LOOP);
		expect(sample_playback(s, SourceId::B).reversed).to_equal(true);
		expect(sample_playback(s, SourceId::A)).to_equal(SamplePlayback{});
	});

	it("reads a slot's ranges in order, and none for a slot without", _ {
		const SoundState s = sample_on_b();
		expect(source_ranges(s, SourceId::A).size()).to_equal(size_t{0});
		expect(source_ranges(s, SourceId::B).size()).to_equal(size_t{1});
		expect(source_ranges(s, SourceId::B)[0].file_path).to_equal(std::string{"SAMPLES/A.WAV"});
	});

	it("reads the DX7's patch, which a sound keeps whichever engine plays", _ {
		SoundState s;
		expect(dx7_patch(s)).to_be_null();
		DxPatchState patch;
		patch.params[0] = 42;
		s = with_dx7_patch(std::move(s), patch);
		expect(dx7_patch(s)).not_().to_be_null();
		expect(int{dx7_patch(s)->params[0]}).to_equal(42);
	});

	it("writes ranges into one slot and the DX7's patch, leaving the slots", _ {
		const SoundState s = sample_on_b();
		const SoundState ranged = with_ranges(s, SourceId::B, {{.top_note = 72, .file_path = "SAMPLES/B.WAV"}});
		expect(source_ranges(ranged, SourceId::B)[0].top_note).to_equal(int16_t{72});
		expect(same_slot(ranged, s, SourceId::A)).to_equal(true);
		DxPatchState patch;
		patch.params[1] = 9;
		const SoundState dx = with_dx7_patch(s, patch);
		expect(int{dx7_patch(dx)->params[1]}).to_equal(9);
		expect(same_slot(dx, s, SourceId::A)).to_equal(true);
		expect(same_slot(dx, s, SourceId::B)).to_equal(true);
	});

	it("sizes a sound's sources by the source count, one slot per SourceId", _ {
		expect(SoundState{}.recall.slots.size()).to_equal(kSourceCount);
		expect(kSourceCount).to_equal(size_t{2});
		expect(static_cast<size_t>(SourceId::B) + 1).to_equal(kSourceCount);
	});

	it("reads the mode and the sound's effects", _ {
		SoundState s = deluge::test::with(SoundState{}, "mode", to_string(SynthMode::FM));
		sound_effects(s).saturation.clipping_amount = 3;
		expect(synth_mode(s)).to_equal(SynthMode::FM);
		expect(int{sound_effects(s).saturation.clipping_amount}).to_equal(3);
	});
});
// clang-format on

CPPSPEC_SPEC(state_access)
