#include "instruments/deluge/state_access.h"
#include "support/printers/sound_state_printer.h"

#include <cppspec.hpp>
#include <string>

using namespace deluge::instrument;

namespace {

/// A subtractive sound whose first oscillator is tuned up and whose second plays one looped, reversed sample.
SoundState sample_on_b() {
	SoundState s;
	s.sources[0].transpose = 7;
	s.sources[0].cents = -3;
	s.sources[1].osc_type = OscType::SAMPLE;
	s.sources[1].repeat_mode = SampleRepeatMode::LOOP;
	s.sources[1].reversed = true;
	s.sources[1].ranges.push_back({.top_note = 60, .file_path = "SAMPLES/A.WAV", .end_pos = 100});
	return s;
}

} // namespace

// clang-format off
describe state_access("the Deluge instrument's state, as code outside the instrument reads it", ${
	it("reads each slot's type, tuning and playback", _ {
		const SoundState s = sample_on_b();
		expect(source_type(s, SourceId::A)).to_equal(OscType::SINE);
		expect(source_type(s, SourceId::B)).to_equal(OscType::SAMPLE);
		expect(source_tuning(s, SourceId::A) == Tuning{.transpose = 7, .cents = -3}).to_equal(true);
		expect(sample_playback(s, SourceId::B).repeat_mode).to_equal(SampleRepeatMode::LOOP);
		expect(sample_playback(s, SourceId::B).reversed).to_equal(true);
		expect(sample_playback(s, SourceId::A) == SamplePlayback{}).to_equal(true);
	});

	it("reads a slot's ranges in order, and none for a slot without", _ {
		const SoundState s = sample_on_b();
		expect(source_ranges(s, SourceId::A).size()).to_equal(size_t{0});
		expect(source_ranges(s, SourceId::B).size()).to_equal(size_t{1});
		expect(source_ranges(s, SourceId::B)[0].file_path).to_equal(std::string{"SAMPLES/A.WAV"});
	});

	it("reads a DX7 patch only where a slot has one", _ {
		SoundState s;
		expect(dx_patch(s, SourceId::A) == nullptr).to_equal(true);
		s.sources[0].dx_patch.emplace();
		s.sources[0].dx_patch->params[0] = 42;
		expect(dx_patch(s, SourceId::A) == nullptr).to_equal(false);
		expect(int{dx_patch(s, SourceId::A)->params[0]}).to_equal(42);
		expect(dx_patch(s, SourceId::B) == nullptr).to_equal(true);
	});

	it("writes ranges and a DX7 patch into one slot, leaving the rest", _ {
		const SoundState s = sample_on_b();
		const SoundState ranged = with_ranges(s, SourceId::B, {{.top_note = 72, .file_path = "SAMPLES/B.WAV"}});
		expect(source_ranges(ranged, SourceId::B)[0].top_note).to_equal(int16_t{72});
		expect(ranged.sources[0]).to_equal(s.sources[0]);
		DxPatchState patch;
		patch.params[1] = 9;
		const SoundState dx = with_dx_patch(s, SourceId::A, patch);
		expect(int{dx_patch(dx, SourceId::A)->params[1]}).to_equal(9);
		expect(dx.sources[1]).to_equal(s.sources[1]);
	});

	it("sizes a sound's sources by the source count, one slot per SourceId", _ {
		expect(SoundState{}.sources.size()).to_equal(kSourceCount);
		expect(kSourceCount).to_equal(size_t{2});
		expect(static_cast<size_t>(SourceId::B) + 1).to_equal(kSourceCount);
	});

	it("reads the mode and the sound's effects", _ {
		SoundState s;
		s.synth_mode = SynthMode::FM;
		s.effects.saturation.clipping_amount = 3;
		expect(synth_mode(s)).to_equal(SynthMode::FM);
		expect(int{sound_effects(s).saturation.clipping_amount}).to_equal(3);
	});
});
// clang-format on

CPPSPEC_SPEC(state_access)
