#include "instruments/classic/cable_acceptance.h"
#include "instruments/classic/engine_params.h"
#include "instruments/classic/params.h"
#include "instruments/classic/state_access.h"

#include "support/record_state.h"
#include <cppspec.hpp>
#include <string>
#include <utility>

using namespace deluge::instruments::classic;
using deluge::effects::FilterMode;
using deluge::plugin::ParamId;
using deluge::plugin::SyncLevel;

namespace {

SoundState subtractive() {
	return deluge::test::with(SoundState{}, "mode", to_string(SynthMode::SUBTRACTIVE));
}

bool allowed(PatchSource source, ParamId destination, const SoundState& state = subtractive(), bool is_drum = false) {
	return cable_allowed(source, destination, state, is_drum);
}

SoundState with_filters(FilterMode lpf, FilterMode hpf) {
	SoundState state = subtractive();
	sound_effects(state).filter.lpf_mode = lpf;
	sound_effects(state).filter.hpf_mode = hpf;
	return state;
}

} // namespace

// clang-format off
describe cable_acceptance("which sources a Deluge sound lets patch to which params", ${
	it("lets a drum take no note-tracking cable", _ {
		expect(allowed(PatchSource::NOTE, param::kPitch)).to_be_true();
		expect(allowed(PatchSource::NOTE, param::kPitch, subtractive(), true)).to_be_false();
	});

	it("lets no per-voice source reach a param of the whole sound", _ {
		expect(allowed(PatchSource::ENVELOPE_1, param::kLfo1Rate)).to_be_false();
		expect(allowed(PatchSource::LFO_GLOBAL_1, param::kLfo3Rate)).to_be_true();
	});

	it("keeps envelopes and the sidechain off the voice's volume", _ {
		expect(allowed(PatchSource::ENVELOPE_0, param::kVolume)).to_be_false();
		expect(allowed(PatchSource::SIDECHAIN, param::kVolume)).to_be_false();
		expect(allowed(PatchSource::VELOCITY, param::kVolume)).to_be_true();
	});

	it("follows the synth mode", _ {
		const SoundState fm = deluge::test::with(subtractive(), "mode", to_string(SynthMode::FM));
		const SoundState ringmod = deluge::test::with(subtractive(), "mode", to_string(SynthMode::RINGMOD));
		expect(allowed(PatchSource::VELOCITY, param::kOscAPhaseWidth, fm)).to_be_false();
		expect(allowed(PatchSource::VELOCITY, param::kOscAPhaseWidth)).to_be_true();
		expect(allowed(PatchSource::VELOCITY, param::kOscBVolume, ringmod)).to_be_false();
		expect(allowed(PatchSource::VELOCITY, param::kCarrier1Feedback)).to_be_false();
		expect(allowed(PatchSource::VELOCITY, param::kCarrier1Feedback, fm)).to_be_true();
		expect(allowed(PatchSource::VELOCITY, param::kModulator2Volume)).to_be_false();
		expect(allowed(PatchSource::VELOCITY, param::kNoiseVolume, fm)).to_be_false();
	});

	it("lets no cable reach a filter that is off", _ {
		expect(allowed(PatchSource::VELOCITY, param::kLpfFrequency,
		               with_filters(FilterMode::OFF, FilterMode::TRANSISTOR_24DB))).to_be_false();
		expect(allowed(PatchSource::VELOCITY, param::kHpfMorph,
		               with_filters(FilterMode::TRANSISTOR_24DB, FilterMode::OFF))).to_be_false();
		expect(allowed(PatchSource::VELOCITY, param::kHpfMorph,
		               with_filters(FilterMode::TRANSISTOR_24DB, FilterMode::TRANSISTOR_24DB))).to_be_true();
	});

	it("lets a synced LFO's rate take no cable", _ {
		SoundState synced = deluge::test::with(subtractive(), "lfo1/syncLevel", std::to_string(std::to_underlying(SyncLevel::QUARTER)));
		expect(allowed(PatchSource::LFO_GLOBAL_2, param::kLfo1Rate, synced)).to_be_false();
		expect(allowed(PatchSource::LFO_GLOBAL_2, param::kLfo3Rate, synced)).to_be_true();
		deluge::test::set(synced, "lfo3/syncLevel", std::to_string(std::to_underlying(SyncLevel::QUARTER)));
		expect(allowed(PatchSource::LFO_GLOBAL_1, param::kLfo3Rate, synced)).to_be_false();
	});

	it("keeps the post-FX volume manual, the reverb send the sidechain's, and X off pitch", _ {
		expect(allowed(PatchSource::SIDECHAIN, param::kVolumePostFX)).to_be_false();
		expect(allowed(PatchSource::LFO_GLOBAL_1, param::kVolumePostReverbSend)).to_be_false();
		expect(allowed(PatchSource::SIDECHAIN, param::kVolumePostReverbSend)).to_be_true();
		expect(allowed(PatchSource::X, param::kPitch)).to_be_false();
		expect(allowed(PatchSource::Y, param::kPitch)).to_be_true();
	});

	it("lets no cable reach a param of an engine that is not playing", _ {
		const SoundState dx7 = deluge::test::with(subtractive(), "mode", to_string(SynthMode::DX7));
		for (const ParamId id : kOscillatorEngineParams) {
			expect(allowed(PatchSource::LFO_GLOBAL_1, id, dx7)).to_equal(false);
		}
		expect(allowed(PatchSource::LFO_GLOBAL_1, param::kPitch, dx7)).to_equal(true);
		expect(allowed(PatchSource::LFO_GLOBAL_1, param::kLpfFrequency, dx7)).to_equal(true);
	});

	it("lets a cable reach the DX7's params only in DX7 mode", _ {
		const SoundState dx7 = deluge::test::with(subtractive(), "mode", to_string(SynthMode::DX7));
		for (const ParamId id : {param::kDx7Level, param::kDx7AmpMod, param::kDx7Pitch}) {
			for (const SynthMode mode : {SynthMode::SUBTRACTIVE, SynthMode::FM, SynthMode::RINGMOD}) {
				const SoundState other = deluge::test::with(subtractive(), "mode", to_string(mode));
				expect(allowed(PatchSource::VELOCITY, id, other)).to_equal(false);
				expect(allowed(PatchSource::LFO_GLOBAL_1, id, other)).to_equal(false);
			}
			expect(allowed(PatchSource::VELOCITY, id, dx7)).to_equal(true);
			expect(allowed(PatchSource::ENVELOPE_1, id, dx7)).to_equal(true);
			// A drum takes no note-tracking cable, onto these as onto oscillator A's
			expect(allowed(PatchSource::NOTE, id, dx7, true)).to_equal(false);
		}
	});

	it("gives each DX7 param, in DX7 mode, exactly the sources its oscillator A counterpart takes in subtractive", _ {
		const std::pair<ParamId, ParamId> kCounterparts[] = {{param::kDx7Level, param::kOscAVolume},
		                                                     {param::kDx7AmpMod, param::kOscAPhaseWidth},
		                                                     {param::kDx7Pitch, param::kOscAPitch}};
		const SoundState dx7 = deluge::test::with(subtractive(), "mode", to_string(SynthMode::DX7));
		for (const auto& [id, osc] : kCounterparts) {
			for (const bool drum : {false, true}) {
				for (uint8_t s = 0; s < static_cast<uint8_t>(PatchSource::NONE); ++s) {
					const auto source = static_cast<PatchSource>(s);
					expect(allowed(source, id, dx7, drum)).to_equal(allowed(source, osc, subtractive(), drum));
				}
			}
		}
	});
});

CPPSPEC_SPEC(cable_acceptance)
