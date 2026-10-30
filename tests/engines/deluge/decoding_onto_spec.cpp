#include "engines/deluge/codec.h"
#include "serialization/tree.h"

#include <cppspec.hpp>
#include <ostream>
#include <string_view>
#include <vector>

using namespace deluge::instrument;
using deluge::serialization::Value;

namespace deluge::instrument {
/// A state, printed as its dump, so a failing comparison shows it.
std::ostream& operator<<(std::ostream& out, const SoundState& state) {
	return out << deluge::serialization::first_difference(dump_state(state), Value::makeMap());
}

/// A range, printed as its fields, so a failing comparison of range lists shows it.
std::ostream& operator<<(std::ostream& out, const RangeState& range) {
	return out << "{top " << range.top_note << " " << range.file_path << " " << range.start_pos << "-" << range.end_pos
	           << "}";
}
} // namespace deluge::instrument

namespace {

constexpr deluge::plugin::CodecContext kContext{.input_tick_magnitude = 1};

/// A subtractive sound whose first source plays two sample ranges and whose second is a saw with a stale range.
SoundState sampled() {
	SoundState state = new_synth_state();
	SourceState& sample = state.sources[0];
	sample.osc_type = OscType::SAMPLE;
	sample.repeat_mode = SampleRepeatMode::LOOP;
	sample.reversed = true;
	sample.ranges = {RangeState{.top_note = 60, .file_path = "SAMPLES/LOW.WAV", .start_pos = 10, .end_pos = 900},
	                 RangeState{.top_note = 32767, .file_path = "SAMPLES/HIGH.WAV", .start_pos = 0, .end_pos = 700}};
	state.osc_retrigger_phase[0] = 12345;
	SourceState& saw = state.sources[1];
	saw.osc_type = OscType::SAW;
	saw.ranges = {RangeState{.top_note = 32767, .file_path = "SAMPLES/OLD.WAV"}};
	return state;
}

} // namespace

// clang-format off
describe decoding_onto("a slot decoded onto a sound's current state", ${
	it("reads back a subtractive sound's state as it is", _ {
		const SoundState state = sampled();
		const auto decoded = decode_onto(encode(state, kContext), state, kContext);
		expect(decoded.has_value()).to_be_true();
		expect(*decoded).to_equal(state);
	});

	it("keeps an FM sound's oscillator types, samples and DX patch, which no file holds", _ {
		SoundState state = sampled();
		state.synth_mode = SynthMode::FM;
		state.sources[1].osc_type = OscType::DX7;
		state.sources[1].dx_patch = DxPatchState{.params = {}, .engine_mode = 1, .random_detune = 3};
		const auto decoded = decode_onto(encode(state, kContext), state, kContext);
		expect(decoded.has_value()).to_be_true();
		expect(*decoded).to_equal(state);
	});

	it("keeps a non-FM sound's modulators", _ {
		SoundState state = sampled();
		state.modulator_transpose = {5, -7};
		state.modulator1_to_modulator0 = true;
		const auto decoded = decode_onto(encode(state, kContext), state, kContext);
		expect(decoded->modulator_transpose[0]).to_equal(int16_t{5});
		expect(decoded->modulator_transpose[1]).to_equal(int16_t{-7});
		expect(decoded->modulator1_to_modulator0).to_be_true();
	});

	it("keeps a ring-mod sound's sample oscillator, which only reading a file clamps", _ {
		SoundState state = sampled();
		state.synth_mode = SynthMode::RINGMOD;
		const auto decoded = decode_onto(encode(state, kContext), state, kContext);
		expect(decoded->sources[0].osc_type).to_equal(OscType::SAMPLE);
		SoundState fresh = new_synth_state();
		expect(decode(encode(state, kContext), fresh, kContext).has_value()).to_be_true();
		expect(fresh.sources[0].osc_type).to_equal(OscType::WAVETABLE);
	});

	it("takes every value the element holds", _ {
		SoundState state = sampled();
		SoundState written = state;
		written.voice_priority = deluge::plugin::VoicePriority::HIGH;
		const auto decoded = decode_onto(encode(written, kContext), state, kContext);
		expect(decoded->voice_priority).to_equal(deluge::plugin::VoicePriority::HIGH);
	});

	it("keeps a sample source's own transpose and cents, which a file holds only as its ranges'", _ {
		SoundState state = sampled();
		state.sources[0].transpose = 7;
		state.sources[0].cents = -12;
		const auto decoded = decode_onto(encode(state, kContext), state, kContext);
		expect(*decoded).to_equal(state);
	});

	it("keeps what an unchanged entry says, even where the file could not say it (a sync level)", _ {
		SoundState state = sampled();
		state.lfo_sync_level[0] = static_cast<deluge::plugin::SyncLevel>(1);
		constexpr deluge::plugin::CodecContext kSlow{.input_tick_magnitude = 0};
		const auto decoded = decode_onto(encode(state, kSlow), state, kSlow);
		expect(*decoded).to_equal(state);
	});

	it("reads a changed entry as a file holds it", _ {
		SoundState state = sampled();
		SoundState written = state;
		written.lfo_sync_level[0] = static_cast<deluge::plugin::SyncLevel>(1);
		constexpr deluge::plugin::CodecContext kSlow{.input_tick_magnitude = 0};
		const auto decoded = decode_onto(encode(written, kSlow), state, kSlow);
		expect(static_cast<int32_t>(decoded->lfo_sync_level[0])).to_equal(2); // where magnitude 0's file keeps WHOLE
	});

	it("keeps the ranges a changed oscillator type carries over", _ {
		SoundState state = sampled();
		state.sources[1].ranges = {RangeState{.top_note = 32767, .file_path = "SAMPLES/OLD.WAV", .end_pos = 500}};
		SoundState written = state;
		written.sources[1].osc_type = OscType::SAMPLE;
		const auto decoded = decode_onto(encode(written, kContext), state, kContext);
		expect(decoded->sources[1].ranges).to_equal(state.sources[1].ranges);
	});

	it("reads the ranges a changed oscillator type reshapes", _ {
		SoundState state = sampled();
		SoundState written = state;
		written.sources[0].osc_type = OscType::WAVETABLE;
		written.sources[0].ranges = {RangeState{.top_note = 60}, RangeState{.top_note = 32767}};
		const auto decoded = decode_onto(encode(written, kContext), state, kContext);
		expect(decoded->sources[0].ranges).to_equal(written.sources[0].ranges);
	});

	it("carries no sample range's zone into a wavetable, even one with no file", _ {
		SoundState state = sampled();
		state.sources[0].ranges = {RangeState{.top_note = 32767, .end_pos = 9999999}};
		SoundState written = state;
		written.sources[0].osc_type = OscType::WAVETABLE;
		written.sources[0].ranges = {RangeState{.top_note = 32767}};
		const auto decoded = decode_onto(encode(written, kContext), state, kContext);
		expect(decoded->sources[0].ranges).to_equal(written.sources[0].ranges);
	});

	it("keeps an LFO's sync level the file could not say when only its other settings change", _ {
		SoundState state = sampled();
		state.lfo_sync_level[0] = static_cast<deluge::plugin::SyncLevel>(1);
		SoundState written = state;
		written.lfo_wave_type[0] = LFOType::SAW;
		constexpr deluge::plugin::CodecContext kSlow{.input_tick_magnitude = 0};
		const auto decoded = decode_onto(encode(written, kSlow), state, kSlow);
		expect(decoded->lfo_wave_type[0]).to_equal(LFOType::SAW);
		expect(static_cast<int32_t>(decoded->lfo_sync_level[0])).to_equal(1);
	});

	it("reads a changed effect entry and keeps the others", _ {
		SoundState state = sampled();
		state.effects.delay.type = deluge::effects::DelayType::ANALOG;
		SoundState written = state;
		written.effects.filter.lpf_mode = deluge::effects::FilterMode::SVF_NOTCH;
		const auto change = decode_change(encode(written, kContext), encode(state, kContext), state, kContext);
		expect(change->state.effects.filter.lpf_mode).to_equal(deluge::effects::FilterMode::SVF_NOTCH);
		expect(change->state.effects.delay.type).to_equal(deluge::effects::DelayType::ANALOG);
		expect(change->changed).to_equal(std::vector<std::string_view>{"lpfMode"});
	});

	it("reads whether a stutter uses the song's from the slot", _ {
		SoundState state = sampled();
		state.effects.stutter.use_song_stutter = false;
		SoundState written = state;
		written.effects.stutter.use_song_stutter = true;
		written.effects.stutter.reversed = true;
		const auto decoded = decode_onto(encode(written, kContext), state, kContext);
		expect(decoded->effects.stutter.reversed).to_be_true();
		expect(decoded->effects.stutter.use_song_stutter).to_be_true();
		expect(decode_onto(encode(state, kContext), state, kContext)->effects.stutter.use_song_stutter).to_be_false();
	});

	it("keeps an unchanged effect entry", _ {
		SoundState state = sampled();
		state.effects.filter.lpf_mode = deluge::effects::FilterMode::SVF_NOTCH;
		const auto decoded = decode_onto(encode(state, kContext), state, kContext);
		expect(decoded->effects.filter.lpf_mode).to_equal(deluge::effects::FilterMode::SVF_NOTCH);
	});

	it("tells which entries a change to the slot touches, and reads them as decoding onto the state does", _ {
		SoundState state = sampled();
		SoundState written = state;
		written.lfo_wave_type[1] = LFOType::SAW;
		written.voice_priority = deluge::plugin::VoicePriority::HIGH;
		const Value element = encode(written, kContext);
		const auto change = decode_change(element, encode(state, kContext), state, kContext);
		expect(change->changed).to_equal(std::vector<std::string_view>{"voicePriority", "lfo2"});
		expect(change->state).to_equal(*decode_onto(element, state, kContext));
	});
});

CPPSPEC_SPEC(decoding_onto)
