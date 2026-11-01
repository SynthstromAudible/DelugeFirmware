#include "instruments/deluge/cable_acceptance.h"
#include "instruments/deluge/params.h"

#include <cppspec.hpp>

using namespace deluge::instrument;
using deluge::effects::FilterMode;
using deluge::plugin::ParamId;
using deluge::plugin::SyncLevel;

namespace {

SoundState subtractive() {
	SoundState state;
	state.synth_mode = SynthMode::SUBTRACTIVE;
	return state;
}

bool allowed(PatchSource source, ParamId destination, const SoundState& state = subtractive(), bool is_drum = false) {
	return cable_allowed(source, destination, state, is_drum);
}

SoundState with_filters(FilterMode lpf, FilterMode hpf) {
	SoundState state = subtractive();
	state.effects.filter.lpf_mode = lpf;
	state.effects.filter.hpf_mode = hpf;
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
		SoundState fm = subtractive();
		fm.synth_mode = SynthMode::FM;
		SoundState ringmod = subtractive();
		ringmod.synth_mode = SynthMode::RINGMOD;
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
		SoundState synced = subtractive();
		synced.lfo_sync_level[0] = SyncLevel::QUARTER;
		expect(allowed(PatchSource::LFO_GLOBAL_2, param::kLfo1Rate, synced)).to_be_false();
		expect(allowed(PatchSource::LFO_GLOBAL_2, param::kLfo3Rate, synced)).to_be_true();
		synced.lfo_sync_level[2] = SyncLevel::QUARTER;
		expect(allowed(PatchSource::LFO_GLOBAL_1, param::kLfo3Rate, synced)).to_be_false();
	});

	it("keeps the post-FX volume manual, the reverb send the sidechain's, and X off pitch", _ {
		expect(allowed(PatchSource::SIDECHAIN, param::kVolumePostFX)).to_be_false();
		expect(allowed(PatchSource::LFO_GLOBAL_1, param::kVolumePostReverbSend)).to_be_false();
		expect(allowed(PatchSource::SIDECHAIN, param::kVolumePostReverbSend)).to_be_true();
		expect(allowed(PatchSource::X, param::kPitch)).to_be_false();
		expect(allowed(PatchSource::Y, param::kPitch)).to_be_true();
	});
});

CPPSPEC_SPEC(cable_acceptance)
