#include "instruments/deluge/codec.h"
#include "instruments/deluge/sound_state.h"
#include "support/printers/sound_state_printer.h"

#include <array>
#include <cppspec.hpp>

using namespace deluge::instrument;

// clang-format off
describe sound_state("the Deluge instrument's state for a sound", ${
	it("compares equal field by field, ranges and DX7 patch included", _ {
		SoundState a;
		SoundState b;
		expect(a).to_equal(b);
		b.sources[1].ranges.push_back({.top_note = 60, .file_path = "SAMPLES/A.WAV"});
		expect(a).not_().to_equal(b);
		a.sources[1].ranges.push_back({.top_note = 60, .file_path = "SAMPLES/A.WAV"});
		a.sources[0].dx_patch.emplace();
		expect(a).not_().to_equal(b);
		b.sources[0].dx_patch.emplace();
		expect(a).to_equal(b);
	});

	it("is a new synth by default", _ {
		const SoundState s{};
		expect(s.voice_priority).to_equal(deluge::plugin::VoicePriority::MEDIUM);
		expect(s.max_voice_count).to_equal(uint8_t{8});
		expect(s.side_chain_send_level).to_equal(int32_t{0});
		expect(s.output_midi_channel).to_equal(uint8_t{255});
		expect(s.output_midi_note_for_drum).to_equal(uint8_t{255});
		expect(s.oscillator_sync).to_equal(false);
		expect(s.polyphonic).to_equal(PolyphonyMode::POLY);
		expect(s.synth_mode).to_equal(SynthMode::SUBTRACTIVE);
		expect(s.transpose).to_equal(int16_t{0});
		expect(s.cents).to_equal(int8_t{0});
		expect(s.num_unison).to_equal(uint8_t{1});
		expect(s.unison_detune).to_equal(int8_t{8});
		expect(s.unison_stereo_spread).to_equal(uint8_t{0});
		expect(s.modulator1_to_modulator0).to_equal(false);
		expect(s.modulator_transpose).to_equal(std::array<int16_t, 2>{0, -12});
		expect(s.modulator_cents).to_equal(std::array<int8_t, 2>{});
		expect(s.osc_retrigger_phase).to_equal(std::array<uint32_t, 2>{0xFFFFFFFF, 0xFFFFFFFF});
		for (const LFOType wave : s.lfo_wave_type) {
			expect(wave).to_equal(LFOType::TRIANGLE);
		}
		expect(s.modulator_retrigger_phase).to_equal(std::array<uint32_t, 2>{});
		for (const SourceState& src : s.sources) {
			expect(src.osc_type).to_equal(OscType::SQUARE);
			expect(src.transpose).to_equal(int16_t{0});
			expect(src.cents).to_equal(int8_t{0});
			expect(src.repeat_mode).to_equal(SampleRepeatMode::CUT);
			expect(src.time_stretch_amount).to_equal(int8_t{0});
			expect(src.interpolation_mode).to_equal(deluge::plugin::InterpolationMode::SMOOTH);
			expect(src.pitch_and_speed_independent).to_equal(false);
			expect(src.reversed).to_equal(false);
			expect(src.ranges.size()).to_equal(size_t{0});
			expect(src.dx_patch.has_value()).to_equal(false);
		}
	});

	it("records what a new synth starts as", _ {
		const SoundState s = SoundState{};
		expect(s.max_voice_count).to_equal(uint8_t{8});
		expect(s.output_midi_channel).to_equal(uint8_t{255});
		expect(s.output_midi_note_for_drum).to_equal(uint8_t{255});
		expect(s.polyphonic).to_equal(PolyphonyMode::POLY);
		expect(s.num_unison).to_equal(uint8_t{1});
		expect(s.unison_detune).to_equal(int8_t{8});
		expect(s.modulator_transpose).to_equal(std::array<int16_t, 2>{0, -12});
		expect(s.osc_retrigger_phase).to_equal(std::array<uint32_t, 2>{0xFFFFFFFF, 0xFFFFFFFF});
		for (const LFOType wave : s.lfo_wave_type) {
			expect(wave).to_equal(LFOType::TRIANGLE);
		}
		for (const SourceState& src : s.sources) {
			expect(src.osc_type).to_equal(OscType::SQUARE);
			expect(src.interpolation_mode).to_equal(deluge::plugin::InterpolationMode::SMOOTH);
		}
	});
});
// clang-format on

CPPSPEC_SPEC(sound_state)
