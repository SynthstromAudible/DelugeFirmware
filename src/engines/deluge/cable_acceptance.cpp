#include "engines/deluge/cable_acceptance.h"

#include "engines/deluge/params.h"
#include "engines/effects/words.h"

namespace deluge::instrument {

namespace {

/// Whether @p source is a voice's own (an envelope, a local LFO, the note's expression, velocity, note, random).
constexpr bool is_local(PatchSource source) {
	return source >= PatchSource::ENVELOPE_0 && source != PatchSource::NONE;
}

/// Whether @p id is a param of the whole sound, not of each voice: the post-FX volume and every param after it.
constexpr bool is_sound_wide(engine::ParamId id) {
	return id.value >= param::kVolumePostFX.value;
}

constexpr bool is_envelope(PatchSource source) {
	return source >= PatchSource::ENVELOPE_0 && source <= PatchSource::ENVELOPE_3;
}

} // namespace

bool cable_allowed(PatchSource source, engine::ParamId destination, const SoundState& state, bool is_drum) {
	if (source == PatchSource::NOTE && is_drum) {
		return false;
	}
	if (destination == kAnyDestination) {
		return true;
	}
	if (is_local(source) && is_sound_wide(destination)) {
		return false;
	}
	const bool fm = state.synth_mode == SynthMode::FM;
	const auto is = [&](engine::ParamId id) { return destination == id; };
	if (is(param::kVolume)) {
		return !is_envelope(source) && source != PatchSource::SIDECHAIN;
	}
	if (is(param::kOscAPhaseWidth) || is(param::kOscBPhaseWidth) || is(param::kNoiseVolume)) {
		return !fm;
	}
	if (is(param::kOscAVolume) || is(param::kOscBVolume)) {
		return state.synth_mode != SynthMode::RINGMOD;
	}
	if (is(param::kCarrier1Feedback) || is(param::kCarrier2Feedback) || is(param::kModulator1Volume)
	    || is(param::kModulator2Volume) || is(param::kModulator1Pitch) || is(param::kModulator2Pitch)
	    || is(param::kModulator1Feedback) || is(param::kModulator2Feedback)) {
		return fm;
	}
	if (is(param::kLpfFrequency) || is(param::kLpfMorph) || is(param::kLpfResonance)) {
		return state.effects.filter.lpf_mode != effects::FilterMode::OFF;
	}
	if (is(param::kHpfFrequency) || is(param::kHpfMorph) || is(param::kHpfResonance)) {
		return state.effects.filter.hpf_mode != effects::FilterMode::OFF;
	}
	if (is(param::kLfo1Rate)) {
		return state.lfo_sync_level[0] == engine::SyncLevel::NONE;
	}
	if (is(param::kLfo3Rate)) {
		return state.lfo_sync_level[2] == engine::SyncLevel::NONE;
	}
	if (is(param::kVolumePostFX)) {
		return false;
	}
	if (is(param::kPitch)) {
		return source != PatchSource::X;
	}
	if (is(param::kVolumePostReverbSend)) {
		return source == PatchSource::SIDECHAIN;
	}
	return true;
}

} // namespace deluge::instrument
