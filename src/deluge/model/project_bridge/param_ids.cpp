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

#include "model/project_bridge/param_ids.h"

#include "ui/controls/follow.h"

#include "arp/params.h"

#include "definitions_cxx.hpp"
#include "modulation/patch/patch_cable.h"

#include "host/chains/params.h"
#include "instruments/classic/cables.h"
#include "instruments/classic/params.h"

#include <optional>
#include <span>

namespace deluge::bridge {

namespace params = deluge::modulation::params;
namespace instrument = deluge::instruments::classic;
namespace host_chain = deluge::host::chains;

// The runtime's numbering may move only with the engines' maps; these anchor each run's ends and sizes.
static_assert(instrument::param::kVolumePostFX.value == params::FIRST_GLOBAL);
static_assert(instrument::kExpressionPitchBend.value == X_PITCH_BEND);
static_assert(instrument::kExpressionSlide.value == Y_SLIDE_TIMBRE);
static_assert(instrument::kExpressionPressure.value == Z_PRESSURE);
// Each legacy enumerator names the neutral id at its index; a new enumerator adds its assert.
// patched params
static_assert(instrument::param::kOscAVolume.value == params::LOCAL_OSC_A_VOLUME);
static_assert(instrument::param::kOscBVolume.value == params::LOCAL_OSC_B_VOLUME);
static_assert(instrument::param::kVolume.value == params::LOCAL_VOLUME);
static_assert(instrument::param::kNoiseVolume.value == params::LOCAL_NOISE_VOLUME);
static_assert(instrument::param::kModulator1Volume.value == params::LOCAL_MODULATOR_0_VOLUME);
static_assert(instrument::param::kModulator2Volume.value == params::LOCAL_MODULATOR_1_VOLUME);
static_assert(instrument::param::kWaveFold.value == params::LOCAL_FOLD);
static_assert(instrument::param::kModulator1Feedback.value == params::LOCAL_MODULATOR_0_FEEDBACK);
static_assert(instrument::param::kModulator2Feedback.value == params::LOCAL_MODULATOR_1_FEEDBACK);
static_assert(instrument::param::kCarrier1Feedback.value == params::LOCAL_CARRIER_0_FEEDBACK);
static_assert(instrument::param::kCarrier2Feedback.value == params::LOCAL_CARRIER_1_FEEDBACK);
static_assert(instrument::param::kLpfResonance.value == params::LOCAL_LPF_RESONANCE);
static_assert(instrument::param::kHpfResonance.value == params::LOCAL_HPF_RESONANCE);
static_assert(instrument::param::kEnv1Sustain.value == params::LOCAL_ENV_0_SUSTAIN);
static_assert(instrument::param::kEnv2Sustain.value == params::LOCAL_ENV_1_SUSTAIN);
static_assert(instrument::param::kEnv3Sustain.value == params::LOCAL_ENV_2_SUSTAIN);
static_assert(instrument::param::kEnv4Sustain.value == params::LOCAL_ENV_3_SUSTAIN);
static_assert(instrument::param::kLpfMorph.value == params::LOCAL_LPF_MORPH);
static_assert(instrument::param::kHpfMorph.value == params::LOCAL_HPF_MORPH);
static_assert(instrument::param::kOscAPhaseWidth.value == params::LOCAL_OSC_A_PHASE_WIDTH);
static_assert(instrument::param::kOscBPhaseWidth.value == params::LOCAL_OSC_B_PHASE_WIDTH);
static_assert(instrument::param::kOscAWavetablePosition.value == params::LOCAL_OSC_A_WAVE_INDEX);
static_assert(instrument::param::kOscBWavetablePosition.value == params::LOCAL_OSC_B_WAVE_INDEX);
static_assert(instrument::param::kPan.value == params::LOCAL_PAN);
static_assert(instrument::param::kLpfFrequency.value == params::LOCAL_LPF_FREQ);
static_assert(instrument::param::kPitch.value == params::LOCAL_PITCH_ADJUST);
static_assert(instrument::param::kOscAPitch.value == params::LOCAL_OSC_A_PITCH_ADJUST);
static_assert(instrument::param::kOscBPitch.value == params::LOCAL_OSC_B_PITCH_ADJUST);
static_assert(instrument::param::kModulator1Pitch.value == params::LOCAL_MODULATOR_0_PITCH_ADJUST);
static_assert(instrument::param::kModulator2Pitch.value == params::LOCAL_MODULATOR_1_PITCH_ADJUST);
static_assert(instrument::param::kHpfFrequency.value == params::LOCAL_HPF_FREQ);
static_assert(instrument::param::kLfo2Rate.value == params::LOCAL_LFO_LOCAL_FREQ_1);
static_assert(instrument::param::kLfo4Rate.value == params::LOCAL_LFO_LOCAL_FREQ_2);
static_assert(instrument::param::kEnv1Attack.value == params::LOCAL_ENV_0_ATTACK);
static_assert(instrument::param::kEnv2Attack.value == params::LOCAL_ENV_1_ATTACK);
static_assert(instrument::param::kEnv3Attack.value == params::LOCAL_ENV_2_ATTACK);
static_assert(instrument::param::kEnv4Attack.value == params::LOCAL_ENV_3_ATTACK);
static_assert(instrument::param::kEnv1Decay.value == params::LOCAL_ENV_0_DECAY);
static_assert(instrument::param::kEnv2Decay.value == params::LOCAL_ENV_1_DECAY);
static_assert(instrument::param::kEnv3Decay.value == params::LOCAL_ENV_2_DECAY);
static_assert(instrument::param::kEnv4Decay.value == params::LOCAL_ENV_3_DECAY);
static_assert(instrument::param::kEnv1Release.value == params::LOCAL_ENV_0_RELEASE);
static_assert(instrument::param::kEnv2Release.value == params::LOCAL_ENV_1_RELEASE);
static_assert(instrument::param::kEnv3Release.value == params::LOCAL_ENV_2_RELEASE);
static_assert(instrument::param::kEnv4Release.value == params::LOCAL_ENV_3_RELEASE);
static_assert(instrument::param::kVolumePostFX.value == params::GLOBAL_VOLUME_POST_FX);
static_assert(instrument::param::kVolumePostReverbSend.value == params::GLOBAL_VOLUME_POST_REVERB_SEND);
static_assert(instrument::param::kReverbAmount.value == params::GLOBAL_REVERB_AMOUNT);
static_assert(instrument::param::kModFXDepth.value == params::GLOBAL_MOD_FX_DEPTH);
static_assert(instrument::param::kDelayFeedback.value == params::GLOBAL_DELAY_FEEDBACK);
static_assert(instrument::param::kDelayRate.value == params::GLOBAL_DELAY_RATE);
static_assert(instrument::param::kModFXRate.value == params::GLOBAL_MOD_FX_RATE);
static_assert(instrument::param::kLfo1Rate.value == params::GLOBAL_LFO_FREQ_1);
static_assert(instrument::param::kLfo3Rate.value == params::GLOBAL_LFO_FREQ_2);
static_assert(instrument::param::kArpRate.value == params::GLOBAL_ARP_RATE);
// a sound's unpatched params
static_assert(instrument::param::kStutterRate.value == params::UNPATCHED_START + params::UNPATCHED_STUTTER_RATE);
static_assert(instrument::param::kBass.value == params::UNPATCHED_START + params::UNPATCHED_BASS);
static_assert(instrument::param::kTreble.value == params::UNPATCHED_START + params::UNPATCHED_TREBLE);
static_assert(instrument::param::kBassFreq.value == params::UNPATCHED_START + params::UNPATCHED_BASS_FREQ);
static_assert(instrument::param::kTrebleFreq.value == params::UNPATCHED_START + params::UNPATCHED_TREBLE_FREQ);
static_assert(instrument::param::kSampleRateReduction.value
              == params::UNPATCHED_START + params::UNPATCHED_SAMPLE_RATE_REDUCTION);
static_assert(instrument::param::kBitcrushAmount.value == params::UNPATCHED_START + params::UNPATCHED_BITCRUSHING);
static_assert(instrument::param::kModFXOffset.value == params::UNPATCHED_START + params::UNPATCHED_MOD_FX_OFFSET);
static_assert(instrument::param::kModFXFeedback.value == params::UNPATCHED_START + params::UNPATCHED_MOD_FX_FEEDBACK);
static_assert(instrument::param::kCompressorShape.value == params::UNPATCHED_START + params::UNPATCHED_SIDECHAIN_SHAPE);
static_assert(instrument::param::kCompressorThreshold.value
              == params::UNPATCHED_START + params::UNPATCHED_COMPRESSOR_THRESHOLD);
static_assert(instrument::param::kPortamento.value == params::UNPATCHED_START + params::UNPATCHED_PORTAMENTO);
// a global effectable's unpatched params
static_assert(host_chain::param::kStutterRate.value == params::UNPATCHED_START + params::UNPATCHED_STUTTER_RATE);
static_assert(host_chain::param::kBass.value == params::UNPATCHED_START + params::UNPATCHED_BASS);
static_assert(host_chain::param::kTreble.value == params::UNPATCHED_START + params::UNPATCHED_TREBLE);
static_assert(host_chain::param::kBassFreq.value == params::UNPATCHED_START + params::UNPATCHED_BASS_FREQ);
static_assert(host_chain::param::kTrebleFreq.value == params::UNPATCHED_START + params::UNPATCHED_TREBLE_FREQ);
static_assert(host_chain::param::kSampleRateReduction.value
              == params::UNPATCHED_START + params::UNPATCHED_SAMPLE_RATE_REDUCTION);
static_assert(host_chain::param::kBitcrushAmount.value == params::UNPATCHED_START + params::UNPATCHED_BITCRUSHING);
static_assert(host_chain::param::kModFXOffset.value == params::UNPATCHED_START + params::UNPATCHED_MOD_FX_OFFSET);
static_assert(host_chain::param::kModFXFeedback.value == params::UNPATCHED_START + params::UNPATCHED_MOD_FX_FEEDBACK);
static_assert(host_chain::param::kCompressorShape.value == params::UNPATCHED_START + params::UNPATCHED_SIDECHAIN_SHAPE);
static_assert(host_chain::param::kCompressorThreshold.value
              == params::UNPATCHED_START + params::UNPATCHED_COMPRESSOR_THRESHOLD);
static_assert(host_chain::param::kModFXRate.value == params::UNPATCHED_START + params::UNPATCHED_MOD_FX_RATE);
static_assert(host_chain::param::kModFXDepth.value == params::UNPATCHED_START + params::UNPATCHED_MOD_FX_DEPTH);
static_assert(host_chain::param::kDelayRate.value == params::UNPATCHED_START + params::UNPATCHED_DELAY_RATE);
static_assert(host_chain::param::kDelayFeedback.value == params::UNPATCHED_START + params::UNPATCHED_DELAY_AMOUNT);
static_assert(host_chain::param::kPan.value == params::UNPATCHED_START + params::UNPATCHED_PAN);
static_assert(host_chain::param::kLpfFrequency.value == params::UNPATCHED_START + params::UNPATCHED_LPF_FREQ);
static_assert(host_chain::param::kLpfResonance.value == params::UNPATCHED_START + params::UNPATCHED_LPF_RES);
static_assert(host_chain::param::kLpfMorph.value == params::UNPATCHED_START + params::UNPATCHED_LPF_MORPH);
static_assert(host_chain::param::kHpfFrequency.value == params::UNPATCHED_START + params::UNPATCHED_HPF_FREQ);
static_assert(host_chain::param::kHpfResonance.value == params::UNPATCHED_START + params::UNPATCHED_HPF_RES);
static_assert(host_chain::param::kHpfMorph.value == params::UNPATCHED_START + params::UNPATCHED_HPF_MORPH);
static_assert(host_chain::param::kReverbAmount.value == params::UNPATCHED_START + params::UNPATCHED_REVERB_SEND_AMOUNT);
static_assert(host_chain::param::kVolume.value == params::UNPATCHED_START + params::UNPATCHED_VOLUME);
static_assert(host_chain::param::kSidechainCompressorVolume.value
              == params::UNPATCHED_START + params::UNPATCHED_SIDECHAIN_VOLUME);
static_assert(host_chain::param::kPitchAdjust.value == params::UNPATCHED_START + params::UNPATCHED_PITCH_ADJUST);
static_assert(host_chain::param::kTempo.value == params::UNPATCHED_START + params::UNPATCHED_TEMPO);
static_assert(instrument::kParamBlocks[0].count == params::kNumParams);
static_assert(instrument::kUnpatchedFirstId == params::UNPATCHED_START);
static_assert(instrument::kParamBlocks[1].count == params::UNPATCHED_SOUND_MAX_NUM);
static_assert(host_chain::kFirstId == params::UNPATCHED_START);
static_assert(host_chain::kParamBlock.count == params::UNPATCHED_GLOBAL_MAX_NUM);
static_assert(instrument::kParamBlocks.size() <= deluge::project::kEngineParamBlocks);
// An unpatched set not yet named (kind NONE) is numbered and stored the same whichever kind it becomes.
static_assert(instrument::kUnpatchedFirstId == host_chain::kFirstId);

static_assert(static_cast<int>(instrument::Polarity::UNIPOLAR) == static_cast<int>(::Polarity::UNIPOLAR));
static_assert(static_cast<int>(instrument::Polarity::BIPOLAR) == static_cast<int>(::Polarity::BIPOLAR));

deluge::plugin::ParamId cableDepthIdOf(uint32_t word) {
	return instrument::cable_depth_of_word(word);
}

uint32_t cableWordOf(deluge::plugin::ParamId depth) {
	return instrument::cable_word_of(depth);
}

deluge::plugin::ParamId paramIdOf(params::Kind kind, int32_t index) {
	const auto at = static_cast<uint32_t>(index);
	if (kind == params::Kind::PATCH_CABLE) { // the index is the runtime's packed cable word
		return cableDepthIdOf(at);
	}
	// An unpatched set reports NONE until its owner's init names it; runtime_param_id numbers it as unpatched.
	return instrument::runtime_param_id(kind, at).value_or(deluge::plugin::ParamId{at});
}

std::optional<int32_t> runtimeIndexOf(params::Kind kind, deluge::plugin::ParamId id) {
	switch (kind) {
	case params::Kind::PATCHED:
	case params::Kind::UNPATCHED_SOUND: {
		std::optional<instrument::RuntimeSlot> slot = instrument::runtime_slot(id);
		const auto set =
		    kind == params::Kind::PATCHED ? instrument::RuntimeSet::PATCHED : instrument::RuntimeSet::UNPATCHED;
		if (!slot || slot->set != set) {
			return std::nullopt;
		}
		return slot->index;
	}
	case params::Kind::NONE:
		// An unpatched set its owner's init has not named yet may be either kind's; both number from the same first id.
		if (std::optional<int32_t> index = runtimeIndexOf(params::Kind::UNPATCHED_SOUND, id)) {
			return index;
		}
		return runtimeIndexOf(params::Kind::UNPATCHED_GLOBAL, id);
	case params::Kind::UNPATCHED_GLOBAL:
		if (std::optional<uint8_t> index = host_chain::find_runtime_index(id)) {
			return *index;
		}
		return std::nullopt;
	case params::Kind::PATCH_CABLE:
		return static_cast<int32_t>(cableWordOf(id));
	default:
		return static_cast<int32_t>(id.value);
	}
}

std::optional<RuntimeParam> runtimeParamOf(OwnerPlugin owner, deluge::project::ParamTarget target) {
	switch (target.space) {
	case deluge::project::ParamSpace::MIDI_CC:
		return RuntimeParam{params::Kind::MIDI, static_cast<int32_t>(target.id.value)};
	case deluge::project::ParamSpace::EXPRESSION:
		return RuntimeParam{params::Kind::EXPRESSION, static_cast<int32_t>(target.id.value)};
	case deluge::project::ParamSpace::ARP:
		if (target.id.value >= deluge::arp::kParamCount) {
			return std::nullopt;
		}
		return RuntimeParam{params::Kind::ARP, static_cast<int32_t>(target.id.value)};
	case deluge::project::ParamSpace::ENGINE:
		break;
	}
	if (owner == OwnerPlugin::GLOBAL_EFFECTABLE) {
		std::optional<uint8_t> index = host_chain::find_runtime_index(target.id);
		if (!index) {
			return std::nullopt;
		}
		return RuntimeParam{params::Kind::UNPATCHED_GLOBAL, *index};
	}
	if (instrument::is_cable_depth(target.id)) {
		return RuntimeParam{params::Kind::PATCH_CABLE, static_cast<int32_t>(cableWordOf(target.id))};
	}
	std::optional<instrument::RuntimeSlot> slot = instrument::runtime_slot(target.id);
	if (!slot) {
		return std::nullopt;
	}
	return RuntimeParam{slot->set == instrument::RuntimeSet::PATCHED ? params::Kind::PATCHED
	                                                                 : params::Kind::UNPATCHED_SOUND,
	                    slot->index};
}

std::optional<deluge::project::ParamTarget> targetOf(OwnerPlugin owner, params::Kind kind, int32_t index) {
	if (kind == params::Kind::NONE) { // an unpatched set its owner's init has not named yet
		kind = owner == OwnerPlugin::INSTRUMENT ? params::Kind::UNPATCHED_SOUND : params::Kind::UNPATCHED_GLOBAL;
	}
	const deluge::plugin::ParamId id = paramIdOf(kind, index);
	deluge::project::PluginParam turned{.target = deluge::project::engine_target(id.value)};
	switch (kind) {
	case params::Kind::MIDI:
		turned.target.space = deluge::project::ParamSpace::MIDI_CC;
		break;
	case params::Kind::EXPRESSION:
		turned.target.space = deluge::project::ParamSpace::EXPRESSION;
		break;
	case params::Kind::ARP:
		turned.target.space = deluge::project::ParamSpace::ARP;
		break;
	case params::Kind::PATCH_CABLE:
		// A cable is the instrument's, and its depth is in no table: what the word names is the target as it stands.
		return owner == OwnerPlugin::INSTRUMENT ? std::optional{turned.target} : std::nullopt;
	case params::Kind::PATCHED:
	case params::Kind::UNPATCHED_SOUND:
		turned.plugin = OwnerPlugin::INSTRUMENT;
		break;
	case params::Kind::UNPATCHED_GLOBAL:
		turned.plugin = OwnerPlugin::GLOBAL_EFFECTABLE;
		break;
	case params::Kind::NONE:
	case params::Kind::STATIC:
	default: // a kind with no follow, or a value that is no kind
		return std::nullopt;
	}
	return deluge::ui::controls::follow_target(owner, turned, instrument::has_engine_param);
}

deluge::plugin::ParamId firstParamId(params::Kind kind) {
	return paramIdOf(kind, 0);
}

deluge::project::ParamSpace spaceOf(params::Kind kind) {
	switch (kind) {
	case params::Kind::EXPRESSION:
		return deluge::project::ParamSpace::EXPRESSION;
	case params::Kind::MIDI:
		return deluge::project::ParamSpace::MIDI_CC;
	case params::Kind::ARP:
		return deluge::project::ParamSpace::ARP;
	default:
		return deluge::project::ParamSpace::ENGINE;
	}
}

std::optional<deluge::project::DocParamSet>* storeFor(deluge::project::DocParamManager& doc, params::Kind kind) {
	switch (kind) {
	case params::Kind::PATCHED:
		return &doc.engine_params[0];
	case params::Kind::NONE: // an unpatched set its owner's init has not named yet
	case params::Kind::UNPATCHED_SOUND:
	case params::Kind::UNPATCHED_GLOBAL:
		return &doc.engine_params[1];
	case params::Kind::EXPRESSION:
		return &doc.expression;
	case params::Kind::ARP:
		return &doc.arp;
	default:
		return nullptr;
	}
}

const std::optional<deluge::project::DocParamSet>* storeFor(const deluge::project::DocParamManager& doc,
                                                            params::Kind kind) {
	return storeFor(const_cast<deluge::project::DocParamManager&>(doc), kind);
}

deluge::project::ParamAddress addressOf(deluge::project::OwnerId owner, params::Kind kind, int32_t index,
                                        std::optional<uint32_t> clip_id) {
	return {owner, spaceOf(kind), paramIdOf(kind, index), clip_id};
}

} // namespace deluge::bridge
