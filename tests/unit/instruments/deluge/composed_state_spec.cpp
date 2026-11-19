#include "instruments/deluge/composed_state.h"
#include "instruments/deluge/dx7_init_voice.h"
#include "support/flat_state.h"

#include <cppspec.hpp>
#include <variant>

#include "support/spec_main.h"

using namespace deluge::instrument;

namespace {

/// Whether @p flat survives being composed and flattened again.
bool round_trips(const FlatSoundState& flat) {
	const auto [state, recall] = compose(flat);
	return flatten(state, recall) == flat;
}

/// A sound whose slots hold the given kinds, each with some settings of its own and some of the other kinds'.
FlatSoundState sound_of(SynthMode mode, OscType a, OscType b) {
	FlatSoundState s;
	s.synth_mode = mode;
	s.sources[0].osc_type = a;
	s.sources[1].osc_type = b;
	for (SourceState& src : s.sources) {
		src.transpose = 5;
		src.cents = -7;
		src.repeat_mode = SampleRepeatMode::LOOP;
		src.reversed = true;
		src.ranges = {{.top_note = 40, .file_path = "SAMPLES/A.WAV", .start_pos = 10, .end_pos = 900},
		              {.top_note = 127, .file_path = "SAMPLES/B.WAV", .end_pos = 400, .transpose = 2}};
		src.dx_patch.emplace(DxPatchState{.params = kDx7InitVoice, .engine_mode = 2, .random_detune = 3});
		if (src.osc_type == OscType::WAVETABLE) {
			// A range holds a zone and tuning only in a sample source (the codec's and the runtime's rule)
			for (RangeState& range : src.ranges) {
				range = RangeState{.top_note = range.top_note, .file_path = range.file_path};
			}
		}
	}
	s.osc_retrigger_phase = {0x40000000, 0xFFFFFFFF};
	s.oscillator_sync = true;
	s.modulator_transpose = {3, -12};
	s.modulator_retrigger_phase = {0xFFFFFFFF, 7};
	s.modulator1_to_modulator0 = true;
	s.lfo_sync_level[1] = deluge::plugin::SyncLevel::EIGHTH;
	return s;
}

} // namespace

// clang-format off
describe composed_state("a sound's flat record as the state it plays and what it keeps for a switch back", ${
	it("composes a new synth into the components' defaults, with nothing to recall", _ {
		const auto [state, recall] = compose(FlatSoundState{});
		expect(state == ComposedState{}).to_equal(true);
		expect(recall == Recall{}).to_equal(true);
	});

	it("round-trips every engine with every pair of oscillator kinds, the other kinds' settings kept", _ {
		const OscType kinds[] = {OscType::SINE, OscType::SUPERSAW, OscType::WAVETABLE, OscType::SAMPLE, OscType::DX7,
		                         OscType::INPUT_L, OscType::INPUT_STEREO};
		for (const SynthMode mode : {SynthMode::SUBTRACTIVE, SynthMode::RINGMOD, SynthMode::FM}) {
			for (const OscType a : kinds) {
				for (const OscType b : kinds) {
					expect(round_trips(sound_of(mode, a, b))).to_equal(true);
				}
			}
		}
	});

	it("reads every engine with every pair of oscillator kinds as its flat layout holds them", _ {
		const OscType kinds[] = {OscType::SINE, OscType::SUPERSAW, OscType::WAVETABLE, OscType::SAMPLE, OscType::DX7,
		                         OscType::INPUT_L, OscType::INPUT_STEREO};
		for (const SynthMode mode : {SynthMode::SUBTRACTIVE, SynthMode::RINGMOD, SynthMode::FM}) {
			for (const OscType a : kinds) {
				for (const OscType b : kinds) {
					expect(deluge::test::reader_difference(SoundState(sound_of(mode, a, b)))).to_equal(std::string{});
				}
			}
		}
	});

	it("plays an FM sound's tuning on its carriers, and keeps its oscillators' kinds to recall", _ {
		const auto [state, recall] = compose(sound_of(SynthMode::FM, OscType::SAMPLE, OscType::DX7));
		const auto& fm = std::get<deluge::synth::FmState>(state.engine);
		expect(int{fm.carriers[0].tuning.transpose}).to_equal(5);
		expect(recall.slots[0].osc_type).to_equal(OscType::SAMPLE);
		expect(recall.slots[0].ranges.size()).to_equal(size_t{2});
		expect(recall.sync).to_equal(true);
	});

	it("keeps the sync for a switch back when the second oscillator is a sample, which the file writes it in", _ {
		const auto [state, recall] = compose(sound_of(SynthMode::SUBTRACTIVE, OscType::SINE, OscType::SAMPLE));
		expect(std::get<deluge::synth::SubtractiveState>(state.engine).oscillators.sync).to_equal(false);
		expect(recall.sync).to_equal(true);
	});

	it("gives a sample oscillator its ranges' tuning and no pitch of its own", _ {
		const auto [state, recall] = compose(sound_of(SynthMode::SUBTRACTIVE, OscType::SAMPLE, OscType::SINE));
		const auto& slot = std::get<deluge::synth::SampleState>(
		    std::get<deluge::synth::SubtractiveState>(state.engine).oscillators.slots[0]);
		expect(slot.ranges.size()).to_equal(size_t{2});
		expect(int{slot.ranges[1].tuning.transpose}).to_equal(2);
		expect(recall.retrigger[0].has_value()).to_equal(true); // the file does not write a sample's retrigger
		expect(int{recall.slots[0].transpose}).to_equal(5);
	});
});
// clang-format on

DELUGE_SPEC(composed_state_spec, composed_state)
