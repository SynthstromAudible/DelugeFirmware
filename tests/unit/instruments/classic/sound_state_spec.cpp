#include "instruments/classic/codec.h"
#include "instruments/classic/sound_state.h"
#include "instruments/classic/state_access.h"
#include "support/printers/sound_record_printer.h"

#include <array>
#include <cppspec.hpp>
#include <utility>

using namespace deluge::instruments::classic;

// clang-format off
describe sound_state("the Deluge instrument's state for a sound", ${
	it("compares equal field by field, ranges and DX7 patch included", _ {
		SoundState a;
		SoundState b;
		expect(a).to_equal(b);
		b = with_ranges(std::move(b), SourceId::B, {deluge::synth::SourceRange{.top_note = 60, .file_path = "SAMPLES/A.WAV"}});
		expect(a).not_().to_equal(b);
		a = with_ranges(std::move(a), SourceId::B, {deluge::synth::SourceRange{.top_note = 60, .file_path = "SAMPLES/A.WAV"}});
		a = with_dx7_patch(std::move(a), DxPatchState{});
		expect(a).not_().to_equal(b);
		b = with_dx7_patch(std::move(b), DxPatchState{});
		expect(a).to_equal(b);
	});

	it("is a new synth by default", _ {
		const SoundState s{};
		expect(voicing(s).priority).to_equal(deluge::plugin::VoicePriority::MEDIUM);
		expect(voicing(s).max_voices).to_equal(uint8_t{8});
		expect(oscillator_sync(s)).to_equal(false);
		expect(voicing(s).polyphony).to_equal(PolyphonyMode::POLY);
		expect(synth_mode(s)).to_equal(SynthMode::SUBTRACTIVE);
		expect(voicing(s).master.transpose).to_equal(int16_t{0});
		expect(voicing(s).master.cents).to_equal(int8_t{0});
		expect(unison(s).count).to_equal(uint8_t{1});
		expect(unison(s).detune).to_equal(int8_t{8});
		expect(unison(s).spread).to_equal(uint8_t{0});
		expect(modulator1_to_modulator0(s)).to_equal(false);
		expect(modulators(s)[0].tuning.transpose).to_equal(int16_t{0});
		expect(modulators(s)[1].tuning.transpose).to_equal(int16_t{-12});
		expect(modulators(s)[0].tuning.cents).to_equal(int8_t{0});
		expect(modulators(s)[1].tuning.cents).to_equal(int8_t{0});
		expect(phase_word(source_retrigger(s, SourceId::A))).to_equal(kFreeRunningPhase);
		expect(phase_word(source_retrigger(s, SourceId::B))).to_equal(kFreeRunningPhase);
		for (size_t l = 0; l < 4; ++l) {
			expect(lfo(s, l).shape).to_equal(LFOType::TRIANGLE);
		}
		expect(phase_word(modulators(s)[0].retrigger)).to_equal(uint32_t{0});
		expect(phase_word(modulators(s)[1].retrigger)).to_equal(uint32_t{0});
		for (const SourceId id : {SourceId::A, SourceId::B}) {
			expect(source_type(s, id)).to_equal(OscType::SQUARE);
			expect(source_tuning(s, id).transpose).to_equal(int16_t{0});
			expect(source_tuning(s, id).cents).to_equal(int8_t{0});
			expect(sample_playback(s, id).repeat_mode).to_equal(SampleRepeatMode::CUT);
			expect(sample_playback(s, id).time_stretch_amount).to_equal(int8_t{0});
			expect(sample_playback(s, id).interpolation).to_equal(deluge::plugin::InterpolationMode::SMOOTH);
			expect(sample_playback(s, id).pitch_and_speed_independent).to_equal(false);
			expect(sample_playback(s, id).reversed).to_equal(false);
			expect(source_ranges(s, id).size()).to_equal(size_t{0});
		}
		expect(dx7_patch(s)).to_be_null();
		expect(phase_word(dx7_pitch(s).retrigger)).to_equal(kFreeRunningPhase);
	});

	it("records what a new synth starts as", _ {
		const SoundState s = SoundState{};
		expect(voicing(s).max_voices).to_equal(uint8_t{8});
		expect(voicing(s).polyphony).to_equal(PolyphonyMode::POLY);
		expect(unison(s).count).to_equal(uint8_t{1});
		expect(unison(s).detune).to_equal(int8_t{8});
		expect(modulators(s)[0].tuning.transpose).to_equal(int16_t{0});
		expect(modulators(s)[1].tuning.transpose).to_equal(int16_t{-12});
		expect(phase_word(source_retrigger(s, SourceId::A))).to_equal(kFreeRunningPhase);
		expect(phase_word(source_retrigger(s, SourceId::B))).to_equal(kFreeRunningPhase);
		for (size_t l = 0; l < 4; ++l) {
			expect(lfo(s, l).shape).to_equal(LFOType::TRIANGLE);
		}
		for (const SourceId id : {SourceId::A, SourceId::B}) {
			expect(source_type(s, id)).to_equal(OscType::SQUARE);
			expect(sample_playback(s, id).interpolation).to_equal(deluge::plugin::InterpolationMode::SMOOTH);
		}
	});
});
// clang-format on

CPPSPEC_SPEC(sound_state)
