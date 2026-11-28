/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "instruments/classic/cable_acceptance.h"

#include "effects/words.h"
#include "instruments/classic/engine_params.h"
#include "instruments/classic/params.h"
#include "instruments/classic/state_access.h"

namespace deluge::instruments::classic {

namespace {

/// Whether @p source is a voice's own (an envelope, a local LFO, the note's expression, velocity, note, random).
constexpr bool is_local(PatchSource source) {
	return source >= PatchSource::ENVELOPE_0 && source != PatchSource::NONE;
}

/// Whether @p id is a param of each voice (its row's scope): the only params a voice's own source reaches.
constexpr bool is_per_voice(plugin::ParamId id) {
	const plugin::ParamInfo* row = plugin::find_param(kParams, id);
	return row != nullptr && row->scope == plugin::ParamScope::PER_VOICE;
}

/// Whether @p source is one of the four envelopes.
constexpr bool is_envelope(PatchSource source) {
	return source >= PatchSource::ENVELOPE_0 && source <= PatchSource::ENVELOPE_3;
}

/// What a cable's acceptance reads of a sound: its engine, its filters' modes and LFO1's and LFO3's sync.
struct CableFacts {
	SynthMode mode;
	effects::FilterMode lpf_mode;
	effects::FilterMode hpf_mode;
	plugin::SyncLevel lfo1_sync;
	plugin::SyncLevel lfo3_sync;
};

bool allowed_by(PatchSource source, plugin::ParamId destination, const CableFacts& state, bool is_drum) {
	if (source == PatchSource::NOTE && is_drum) {
		return false;
	}
	if (destination == kAnyDestination) {
		return true;
	}
	if (is_local(source) && !is_per_voice(destination)) {
		return false;
	}
	// Only the playing engine's params, and the sound's own, take a cable
	if (!param_is_live(state.mode, destination)) {
		return false;
	}
	const bool fm = state.mode == SynthMode::FM;
	// The DX7's own params take the sources their oscillator A counterparts take in DX7 mode
	if (const std::optional<plugin::ParamId> osc_a = osc_a_counterpart(destination)) {
		destination = *osc_a;
	}
	const auto is = [&](plugin::ParamId id) { return destination == id; };
	if (is(param::kVolume)) {
		return !is_envelope(source) && source != PatchSource::SIDECHAIN;
	}
	if (is(param::kOscAPhaseWidth)) {
		return !fm;
	}
	if (is(param::kOscBPhaseWidth) || is(param::kNoiseVolume)) {
		return !fm;
	}
	if (is(param::kOscAVolume)) {
		return state.mode != SynthMode::RINGMOD;
	}
	if (is(param::kOscBVolume)) {
		return state.mode != SynthMode::RINGMOD;
	}
	if (is(param::kCarrier1Feedback) || is(param::kCarrier2Feedback) || is(param::kModulator1Volume)
	    || is(param::kModulator2Volume) || is(param::kModulator1Pitch) || is(param::kModulator2Pitch)
	    || is(param::kModulator1Feedback) || is(param::kModulator2Feedback)) {
		return fm;
	}
	if (is(param::kLpfFrequency) || is(param::kLpfMorph) || is(param::kLpfResonance)) {
		return state.lpf_mode != effects::FilterMode::OFF;
	}
	if (is(param::kHpfFrequency) || is(param::kHpfMorph) || is(param::kHpfResonance)) {
		return state.hpf_mode != effects::FilterMode::OFF;
	}
	if (is(param::kLfo1Rate)) {
		return state.lfo1_sync == plugin::SyncLevel::NONE;
	}
	if (is(param::kLfo3Rate)) {
		return state.lfo3_sync == plugin::SyncLevel::NONE;
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

} // namespace

bool cable_allowed(PatchSource source, plugin::ParamId destination, const SoundState& state, bool is_drum) {
	return allowed_by(source, destination,
	                  {.mode = synth_mode(state),
	                   .lpf_mode = sound_effects(state).filter.lpf_mode,
	                   .hpf_mode = sound_effects(state).filter.hpf_mode,
	                   .lfo1_sync = lfo(state, 0).sync_level,
	                   .lfo3_sync = lfo(state, 2).sync_level},
	                  is_drum);
}

} // namespace deluge::instruments::classic
