#include "synth/engines/engine_state.h"
#include "synth/modulation/lfo_state.h"
#include "synth/sources/source_state.h"
#include "synth/voice/voicing_state.h"

#include <cppspec.hpp>
#include <variant>

#include "support/spec_main.h"

using namespace deluge::synth;

// clang-format off
describe component_states("what a sound's components save, as a new synth starts", ${
	it("starts both oscillators as free-running square waves, untuned and unsynced", _ {
		const SubtractiveState engine;
		for (const SlotState& slot : engine.oscillators.slots) {
			expect(std::holds_alternative<VaState>(slot)).to_be_true();
			const auto& va = std::get<VaState>(slot);
			expect(va.shape).to_equal(VaShape::SQUARE);
			expect(va.pitch.retrigger.has_value()).to_be_false();
			expect(int{va.pitch.tuning.transpose}).to_equal(0);
		}
		expect(engine.oscillators.sync).to_be_false();
	});

	it("starts FM's second modulator an octave down, both restarting each note at phase 0", _ {
		const FmState fm;
		expect(int{fm.modulators[1].tuning.transpose}).to_equal(-12);
		expect(fm.modulators[0].retrigger.has_value()).to_be_true();
		expect(fm.modulators[1].retrigger.has_value()).to_be_true();
		expect(fm.carriers[0].retrigger.has_value()).to_be_false();
	});

	it("starts the voicing polyphonic, eight voices, one unison part detuned 8", _ {
		const VoicingState voicing;
		expect(voicing.polyphony).to_equal(PolyphonyMode::POLY);
		expect(int{voicing.max_voices}).to_equal(8);
		expect(int{voicing.unison.count}).to_equal(1);
		expect(int{voicing.unison.detune}).to_equal(8);
	});

	it("starts an LFO as a free-running triangle", _ {
		const LfoState lfo;
		expect(lfo.shape).to_equal(LFOType::TRIANGLE);
		expect(lfo.sync_level).to_equal(deluge::plugin::SyncLevel::NONE);
	});

	it("starts a sample oscillator cutting, smooth, forwards", _ {
		const SampleState sample;
		expect(sample.playback.repeat_mode).to_equal(SampleRepeatMode::CUT);
		expect(sample.playback.interpolation).to_equal(deluge::plugin::InterpolationMode::SMOOTH);
		expect(sample.playback.reversed).to_be_false();
		expect(sample.ranges.empty()).to_be_true();
	});
});
// clang-format on

DELUGE_SPEC(component_state_spec, component_states)
