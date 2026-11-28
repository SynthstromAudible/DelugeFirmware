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

#include "gui/l10n/param_names.h"

#include "arp/params.h"

#include "gui/l10n/l10n.h"
#include "host/chains/params.h"
#include "instruments/classic/params.h"

#include <optional>

namespace deluge::l10n {

namespace {

// The names, keyed by neutral id.
/// @brief The name of a patched param, nullopt for an id that names no param.
std::optional<std::string_view> patched_name(plugin::ParamId id) {
	switch (id.value) {
	case instruments::classic::param::kOscAVolume.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_OSC_A_VOLUME);
	case instruments::classic::param::kOscBVolume.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_OSC_B_VOLUME);
	case instruments::classic::param::kVolume.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_VOLUME);
	case instruments::classic::param::kNoiseVolume.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_NOISE_VOLUME);
	case instruments::classic::param::kModulator1Volume.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_MODULATOR_0_VOLUME);
	case instruments::classic::param::kModulator2Volume.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_MODULATOR_1_VOLUME);
	case instruments::classic::param::kWaveFold.value:
		return getView(String::STRING_FOR_WAVEFOLDER);
	case instruments::classic::param::kModulator1Feedback.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_MODULATOR_0_FEEDBACK);
	case instruments::classic::param::kModulator2Feedback.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_MODULATOR_1_FEEDBACK);
	case instruments::classic::param::kCarrier1Feedback.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_CARRIER_0_FEEDBACK);
	case instruments::classic::param::kCarrier2Feedback.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_CARRIER_1_FEEDBACK);
	case instruments::classic::param::kLpfResonance.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_LPF_RESONANCE);
	case instruments::classic::param::kHpfResonance.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_HPF_RESONANCE);
	case instruments::classic::param::kEnv1Sustain.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_ENV_0_SUSTAIN);
	case instruments::classic::param::kEnv2Sustain.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_ENV_1_SUSTAIN);
	case instruments::classic::param::kEnv3Sustain.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_ENV_2_SUSTAIN);
	case instruments::classic::param::kEnv4Sustain.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_ENV_3_SUSTAIN);
	case instruments::classic::param::kLpfMorph.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_LPF_MORPH);
	case instruments::classic::param::kHpfMorph.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_HPF_MORPH);
	case instruments::classic::param::kOscAPhaseWidth.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_OSC_A_PHASE_WIDTH);
	case instruments::classic::param::kOscBPhaseWidth.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_OSC_B_PHASE_WIDTH);
	case instruments::classic::param::kOscAWavetablePosition.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_OSC_A_WAVE_INDEX);
	case instruments::classic::param::kOscBWavetablePosition.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_OSC_B_WAVE_INDEX);
	case instruments::classic::param::kPan.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_PAN);
	case instruments::classic::param::kLpfFrequency.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_LPF_FREQ);
	case instruments::classic::param::kPitch.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_PITCH_ADJUST);
	case instruments::classic::param::kOscAPitch.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_OSC_A_PITCH_ADJUST);
	case instruments::classic::param::kDx7Level.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_DX7_LEVEL);
	case instruments::classic::param::kDx7AmpMod.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_DX7_AMP_MOD);
	case instruments::classic::param::kDx7Pitch.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_DX7_PITCH_ADJUST);
	case instruments::classic::param::kOscBPitch.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_OSC_B_PITCH_ADJUST);
	case instruments::classic::param::kModulator1Pitch.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST);
	case instruments::classic::param::kModulator2Pitch.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_MODULATOR_1_PITCH_ADJUST);
	case instruments::classic::param::kHpfFrequency.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_HPF_FREQ);
	case instruments::classic::param::kLfo2Rate.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_LFO_LOCAL_FREQ_1);
	case instruments::classic::param::kLfo4Rate.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_LFO_LOCAL_FREQ_2);
	case instruments::classic::param::kEnv1Attack.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_ENV_0_ATTACK);
	case instruments::classic::param::kEnv2Attack.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_ENV_1_ATTACK);
	case instruments::classic::param::kEnv3Attack.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_ENV_2_ATTACK);
	case instruments::classic::param::kEnv4Attack.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_ENV_3_ATTACK);
	case instruments::classic::param::kEnv1Decay.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_ENV_0_DECAY);
	case instruments::classic::param::kEnv2Decay.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_ENV_1_DECAY);
	case instruments::classic::param::kEnv3Decay.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_ENV_2_DECAY);
	case instruments::classic::param::kEnv4Decay.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_ENV_3_DECAY);
	case instruments::classic::param::kEnv1Release.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_ENV_0_RELEASE);
	case instruments::classic::param::kEnv2Release.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_ENV_1_RELEASE);
	case instruments::classic::param::kEnv3Release.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_ENV_2_RELEASE);
	case instruments::classic::param::kEnv4Release.value:
		return getView(String::STRING_FOR_PARAM_LOCAL_ENV_3_RELEASE);
	case instruments::classic::param::kVolumePostFX.value:
		return getView(String::STRING_FOR_PARAM_GLOBAL_VOLUME_POST_FX);
	case instruments::classic::param::kVolumePostReverbSend.value:
		return getView(String::STRING_FOR_PARAM_GLOBAL_VOLUME_POST_REVERB_SEND);
	case instruments::classic::param::kReverbAmount.value:
		return getView(String::STRING_FOR_PARAM_GLOBAL_REVERB_AMOUNT);
	case instruments::classic::param::kModFXDepth.value:
		return getView(String::STRING_FOR_PARAM_GLOBAL_MOD_FX_DEPTH);
	case instruments::classic::param::kDelayFeedback.value:
		return getView(String::STRING_FOR_PARAM_GLOBAL_DELAY_FEEDBACK);
	case instruments::classic::param::kDelayRate.value:
		return getView(String::STRING_FOR_PARAM_GLOBAL_DELAY_RATE);
	case instruments::classic::param::kModFXRate.value:
		return getView(String::STRING_FOR_PARAM_GLOBAL_MOD_FX_RATE);
	case instruments::classic::param::kLfo1Rate.value:
		return getView(String::STRING_FOR_PARAM_GLOBAL_LFO_FREQ_1);
	case instruments::classic::param::kLfo3Rate.value:
		return getView(String::STRING_FOR_PARAM_GLOBAL_LFO_FREQ_2);
	case instruments::classic::param::kArpRate.value:
		return getView(String::STRING_FOR_PARAM_GLOBAL_ARP_RATE);
	}
	return std::nullopt;
}

/// @brief The name of a sound's unpatched param, nullopt for an id that names no param.
std::optional<std::string_view> unpatched_sound_name(plugin::ParamId id) {
	switch (id.value) {
	case instruments::classic::param::kStutterRate.value:
		return getView(String::STRING_FOR_STUTTER_RATE);
	case instruments::classic::param::kBass.value:
		return getView(String::STRING_FOR_BASS);
	case instruments::classic::param::kTreble.value:
		return getView(String::STRING_FOR_TREBLE);
	case instruments::classic::param::kBassFreq.value:
		return getView(String::STRING_FOR_BASS_FREQUENCY);
	case instruments::classic::param::kTrebleFreq.value:
		return getView(String::STRING_FOR_TREBLE_FREQUENCY);
	case instruments::classic::param::kSampleRateReduction.value:
		return getView(String::STRING_FOR_DECIMATION);
	case instruments::classic::param::kBitcrushAmount.value:
		return getView(String::STRING_FOR_BITCRUSH);
	case instruments::classic::param::kModFXOffset.value:
		return getView(String::STRING_FOR_MODFX_OFFSET);
	case instruments::classic::param::kModFXFeedback.value:
		return getView(String::STRING_FOR_MODFX_FEEDBACK);
	case instruments::classic::param::kCompressorShape.value:
		return getView(String::STRING_FOR_SIDECHAIN_SHAPE);
	case instruments::classic::param::kCompressorThreshold.value:
		return getView(String::STRING_FOR_THRESHOLD);
	case instruments::classic::param::kPortamento.value:
		return getView(String::STRING_FOR_PORTAMENTO);
	}
	return std::nullopt;
}

/// @brief The name of a global effectable's unpatched param, nullopt for an id that names no param.
std::optional<std::string_view> unpatched_global_name(plugin::ParamId id) {
	switch (id.value) {
	case host::chains::param::kStutterRate.value:
		return getView(String::STRING_FOR_STUTTER_RATE);
	case host::chains::param::kBass.value:
		return getView(String::STRING_FOR_BASS);
	case host::chains::param::kTreble.value:
		return getView(String::STRING_FOR_TREBLE);
	case host::chains::param::kBassFreq.value:
		return getView(String::STRING_FOR_BASS_FREQUENCY);
	case host::chains::param::kTrebleFreq.value:
		return getView(String::STRING_FOR_TREBLE_FREQUENCY);
	case host::chains::param::kSampleRateReduction.value:
		return getView(String::STRING_FOR_DECIMATION);
	case host::chains::param::kBitcrushAmount.value:
		return getView(String::STRING_FOR_BITCRUSH);
	case host::chains::param::kModFXOffset.value:
		return getView(String::STRING_FOR_MODFX_OFFSET);
	case host::chains::param::kModFXFeedback.value:
		return getView(String::STRING_FOR_MODFX_FEEDBACK);
	case host::chains::param::kCompressorShape.value:
		return getView(String::STRING_FOR_SIDECHAIN_SHAPE);
	case host::chains::param::kCompressorThreshold.value:
		return getView(String::STRING_FOR_THRESHOLD);
	case host::chains::param::kModFXRate.value:
		return getView(String::STRING_FOR_MOD_FX_RATE);
	case host::chains::param::kModFXDepth.value:
		return getView(String::STRING_FOR_MOD_FX_DEPTH);
	case host::chains::param::kDelayRate.value:
		return getView(String::STRING_FOR_DELAY_RATE);
	case host::chains::param::kDelayFeedback.value:
		return getView(String::STRING_FOR_DELAY_AMOUNT);
	case host::chains::param::kPan.value:
		return getView(String::STRING_FOR_PAN);
	case host::chains::param::kLpfFrequency.value:
		return getView(String::STRING_FOR_LPF_FREQUENCY);
	case host::chains::param::kLpfResonance.value:
		return getView(String::STRING_FOR_LPF_RESONANCE);
	case host::chains::param::kLpfMorph.value:
		return getView(String::STRING_FOR_LPF_MORPH);
	case host::chains::param::kHpfFrequency.value:
		return getView(String::STRING_FOR_HPF_FREQUENCY);
	case host::chains::param::kHpfResonance.value:
		return getView(String::STRING_FOR_HPF_RESONANCE);
	case host::chains::param::kHpfMorph.value:
		return getView(String::STRING_FOR_HPF_MORPH);
	case host::chains::param::kReverbAmount.value:
		return getView(String::STRING_FOR_REVERB_AMOUNT);
	case host::chains::param::kVolume.value:
		return getView(String::STRING_FOR_MASTER_LEVEL);
	case host::chains::param::kSidechainCompressorVolume.value:
		return getView(String::STRING_FOR_SIDECHAIN_LEVEL);
	case host::chains::param::kPitchAdjust.value:
		return getView(String::STRING_FOR_MASTER_PITCH);
	case host::chains::param::kTempo.value:
		return getView(String::STRING_FOR_TEMPO);
	}
	return std::nullopt;
}

/// @brief The name of an expression axis, nullopt for an id that names no param.
std::optional<std::string_view> expression_name(plugin::ParamId id) {
	switch (id.value) {
	case instruments::classic::kExpressionPitchBend.value:
		return getView(String::STRING_FOR_PITCH_BEND);
	case instruments::classic::kExpressionSlide.value:
		return getView(String::STRING_FOR_MOD_WHEEL);
	case instruments::classic::kExpressionPressure.value:
		return getView(String::STRING_FOR_CHANNEL_PRESSURE);
	}
	return std::nullopt;
}

/// @brief A patched param's short name, nullopt for an id that names no param.
std::optional<std::string_view> patched_short(plugin::ParamId id) {
	switch (id.value) {
	case instruments::classic::param::kOscAVolume.value:
		return "Osc1 level";
	case instruments::classic::param::kOscBVolume.value:
		return "Osc2 level";
	case instruments::classic::param::kVolume.value:
		return "Level";
	case instruments::classic::param::kNoiseVolume.value:
		return "Noise";
	case instruments::classic::param::kModulator1Volume.value:
		return "Mod1 level";
	case instruments::classic::param::kModulator2Volume.value:
		return "Mod2 level";
	case instruments::classic::param::kWaveFold.value:
		return "Wavefold";
	case instruments::classic::param::kModulator1Feedback.value:
		return "Mod1 feed";
	case instruments::classic::param::kModulator2Feedback.value:
		return "Mod2 feed";
	case instruments::classic::param::kCarrier1Feedback.value:
		return "Osc1 feed";
	case instruments::classic::param::kCarrier2Feedback.value:
		return "Osc2 feed";
	case instruments::classic::param::kLpfResonance.value:
		return "LPF reso";
	case instruments::classic::param::kHpfResonance.value:
		return "HPF reso";
	case instruments::classic::param::kEnv1Sustain.value:
		return "Env1 sus";
	case instruments::classic::param::kEnv2Sustain.value:
		return "Env2 sus";
	case instruments::classic::param::kEnv3Sustain.value:
		return "Env3 sus";
	case instruments::classic::param::kEnv4Sustain.value:
		return "Env4 sus";
	case instruments::classic::param::kLpfMorph.value:
		return "LPF Morph";
	case instruments::classic::param::kHpfMorph.value:
		return "HPF Morph";
	case instruments::classic::param::kOscAPhaseWidth.value:
		return "Osc1 PW";
	case instruments::classic::param::kOscBPhaseWidth.value:
		return "Osc2 PW";
	case instruments::classic::param::kOscAWavetablePosition.value:
		return "Osc1 wave";
	case instruments::classic::param::kOscBWavetablePosition.value:
		return "Osc2 wave";
	case instruments::classic::param::kPan.value:
		return "Pan";
	case instruments::classic::param::kLpfFrequency.value:
		return "LPf freq";
	case instruments::classic::param::kPitch.value:
		return "Pitch";
	case instruments::classic::param::kOscAPitch.value:
		return "Osc1 pitch";
	case instruments::classic::param::kDx7Level.value:
		return "DX7 level";
	case instruments::classic::param::kDx7AmpMod.value:
		return "DX7 amp mod";
	case instruments::classic::param::kDx7Pitch.value:
		return "DX7 pitch";
	case instruments::classic::param::kOscBPitch.value:
		return "Osc2 pitch";
	case instruments::classic::param::kModulator1Pitch.value:
		return "Mod1 pitch";
	case instruments::classic::param::kModulator2Pitch.value:
		return "Mod1 pitch";
	case instruments::classic::param::kHpfFrequency.value:
		return "HPF freq";
	case instruments::classic::param::kLfo2Rate.value:
		return "LFO2 rate";
	case instruments::classic::param::kLfo4Rate.value:
		return "LFO4 rate";
	case instruments::classic::param::kEnv1Attack.value:
		return "Env1attack";
	case instruments::classic::param::kEnv2Attack.value:
		return "Env2attack";
	case instruments::classic::param::kEnv3Attack.value:
		return "Env3attack";
	case instruments::classic::param::kEnv4Attack.value:
		return "Env4attack";
	case instruments::classic::param::kEnv1Decay.value:
		return "Env1 decay";
	case instruments::classic::param::kEnv2Decay.value:
		return "Env2 decay";
	case instruments::classic::param::kEnv3Decay.value:
		return "Env3 decay";
	case instruments::classic::param::kEnv4Decay.value:
		return "Env4 decay";
	case instruments::classic::param::kEnv1Release.value:
		return "Env1 rel";
	case instruments::classic::param::kEnv2Release.value:
		return "Env2 rel";
	case instruments::classic::param::kEnv3Release.value:
		return "Env3 rel";
	case instruments::classic::param::kEnv4Release.value:
		return "Env4 rel";
	case instruments::classic::param::kVolumePostFX.value:
		return "POSTFXLVL";
	case instruments::classic::param::kVolumePostReverbSend.value:
		return "Side level";
	case instruments::classic::param::kReverbAmount.value:
		return "Reverb amt";
	case instruments::classic::param::kModFXDepth.value:
		return "ModFXdepth";
	case instruments::classic::param::kDelayFeedback.value:
		return "Delay feed";
	case instruments::classic::param::kDelayRate.value:
		return "Delay rate";
	case instruments::classic::param::kModFXRate.value:
		return "ModFX rate";
	case instruments::classic::param::kLfo1Rate.value:
		return "LFO1 rate";
	case instruments::classic::param::kLfo3Rate.value:
		return "LFO3 rate";
	case instruments::classic::param::kArpRate.value:
		return "Arp. rate";
	}
	return std::nullopt;
}

/// @brief Whether @p mode's family is the ladder the morph param is renamed for.
bool is_family(effects::FilterMode mode, effects::FilterFamily family) {
	return effects::filter_family(mode) == family;
}

/// @brief Whether the param is a low-pass morph.
bool is_lpf_morph(plugin::ParamKind kind, plugin::ParamId id) {
	return (kind == plugin::ParamKind::PATCHED && id == instruments::classic::param::kLpfMorph)
	       || (kind == plugin::ParamKind::UNPATCHED_GLOBAL && id == host::chains::param::kLpfMorph);
}

/// @brief Whether the param is a high-pass morph.
bool is_hpf_morph(plugin::ParamKind kind, plugin::ParamId id) {
	return (kind == plugin::ParamKind::PATCHED && id == instruments::classic::param::kHpfMorph)
	       || (kind == plugin::ParamKind::UNPATCHED_GLOBAL && id == host::chains::param::kHpfMorph);
}

/// @brief An arp param's name (arp/params.h), whoever holds the arp.
std::optional<std::string_view> arp_name(plugin::ParamId id) {
	switch (id.value) {
	case arp::param::kRate.value:
		return getView(String::STRING_FOR_ARP_RATE_MENU_TITLE);
	case arp::param::kGate.value:
		return getView(String::STRING_FOR_ARP_GATE_MENU_TITLE);
	case arp::param::kRhythm.value:
		return getView(String::STRING_FOR_ARP_RHYTHM_MENU_TITLE);
	case arp::param::kSequenceLength.value:
		return getView(String::STRING_FOR_ARP_SEQUENCE_LENGTH_MENU_TITLE);
	case arp::param::kChordPolyphony.value:
		return getView(String::STRING_FOR_ARP_CHORD_POLYPHONY_MENU_TITLE);
	case arp::param::kRatchetAmount.value:
		return getView(String::STRING_FOR_ARP_RATCHETS_MENU_TITLE);
	case arp::param::kNoteProbability.value:
		return getView(String::STRING_FOR_NOTE_PROBABILITY_MENU_TITLE);
	case arp::param::kReverseProbability.value:
		return getView(String::STRING_FOR_REVERSE_PROBABILITY_MENU_TITLE);
	case arp::param::kBassProbability.value:
		return getView(String::STRING_FOR_ARP_BASS_PROBABILITY_MENU_TITLE);
	case arp::param::kSwapProbability.value:
		return getView(String::STRING_FOR_ARP_SWAP_PROBABILITY_MENU_TITLE);
	case arp::param::kGlideProbability.value:
		return getView(String::STRING_FOR_ARP_GLIDE_PROBABILITY_MENU_TITLE);
	case arp::param::kChordProbability.value:
		return getView(String::STRING_FOR_ARP_CHORD_PROBABILITY_MENU_TITLE);
	case arp::param::kRatchetProbability.value:
		return getView(String::STRING_FOR_ARP_RATCHET_PROBABILITY_MENU_TITLE);
	case arp::param::kSpreadGate.value:
		return getView(String::STRING_FOR_ARP_SPREAD_GATE_MENU_TITLE);
	case arp::param::kSpreadOctave.value:
		return getView(String::STRING_FOR_ARP_SPREAD_OCTAVE_MENU_TITLE);
	case arp::param::kSpreadVelocity.value:
		return getView(String::STRING_FOR_SPREAD_VELOCITY_MENU_TITLE);
	}
	return std::nullopt;
}

/// @brief The name of the param @p id in @p plugin's table, or nullopt where the table names none.
std::optional<std::string_view> plugin_param_name(project::OwnerPlugin plugin, plugin::ParamId id) {
	if (plugin == project::OwnerPlugin::GLOBAL_EFFECTABLE) {
		return unpatched_global_name(id);
	}
	return instruments::classic::is_patched(id) ? patched_name(id) : unpatched_sound_name(id);
}

} // namespace

std::string_view expression_param_name(plugin::ParamId id) {
	return expression_name(id).value_or(getView(String::STRING_FOR_NONE));
}

std::string_view arp_param_name(plugin::ParamId id) {
	return arp_name(id).value_or(getView(String::STRING_FOR_NONE));
}

std::string_view param_name(project::OwnerPlugin plugin, plugin::ParamId id) {
	return plugin_param_name(plugin, id).value_or(getView(String::STRING_FOR_NONE));
}

std::string_view param_name(project::OwnerPlugin plugin, plugin::ParamId id, effects::FilterMode lpf,
                            effects::FilterMode hpf) {
	// The ladder filters repurpose morph, so they name it after what it does. The SVF modes really do morph, and a
	// filter that is off has no morph param at all, so both keep the default name.
	const bool chain = plugin == project::OwnerPlugin::GLOBAL_EFFECTABLE;
	if (id == (chain ? host::chains::param::kLpfMorph : instruments::classic::param::kLpfMorph)
	    && is_family(lpf, effects::FilterFamily::LP_LADDER)) {
		return getView(String::STRING_FOR_LPF_DRIVE);
	}
	if (id == (chain ? host::chains::param::kHpfMorph : instruments::classic::param::kHpfMorph)
	    && is_family(hpf, effects::FilterFamily::HP_LADDER)) {
		return getView(String::STRING_FOR_HPF_FM);
	}
	return param_name(plugin, id);
}

std::string_view param_name(plugin::ParamKind kind, plugin::ParamId id) {
	std::optional<std::string_view> name;
	switch (kind) {
	case plugin::ParamKind::PATCHED:
		name = patched_name(id);
		break;
	case plugin::ParamKind::UNPATCHED_SOUND:
		name = unpatched_sound_name(id);
		break;
	case plugin::ParamKind::UNPATCHED_GLOBAL:
		name = unpatched_global_name(id);
		break;
	case plugin::ParamKind::EXPRESSION:
		name = expression_name(id);
		break;
	case plugin::ParamKind::ARP:
		name = arp_name(id);
		break;
	default: // no names: no kind, a static param, a MIDI CC, a cable
		break;
	}
	return name.value_or(getView(String::STRING_FOR_NONE));
}

std::string_view param_name(plugin::ParamKind kind, plugin::ParamId id, effects::FilterMode lpf,
                            effects::FilterMode hpf) {
	// The ladder filters repurpose morph, so they name it after what it does. The SVF modes really do morph, and a
	// filter that is off has no morph param at all, so both keep the default name.
	if (is_lpf_morph(kind, id) && is_family(lpf, effects::FilterFamily::LP_LADDER)) {
		return getView(String::STRING_FOR_LPF_DRIVE);
	}
	if (is_hpf_morph(kind, id) && is_family(hpf, effects::FilterFamily::HP_LADDER)) {
		return getView(String::STRING_FOR_HPF_FM);
	}
	return param_name(kind, id);
}

std::string_view param_name_at(plugin::ParamKind kind, uint32_t index) {
	const std::optional<plugin::ParamId> id = instruments::classic::runtime_param_id(kind, index);
	return id ? param_name(kind, *id) : getView(String::STRING_FOR_NONE);
}

std::string_view param_name_at(plugin::ParamKind kind, uint32_t index, effects::FilterMode lpf,
                               effects::FilterMode hpf) {
	const std::optional<plugin::ParamId> id = instruments::classic::runtime_param_id(kind, index);
	return id ? param_name(kind, *id, lpf, hpf) : getView(String::STRING_FOR_NONE);
}

std::string_view patched_param_short_name(plugin::ParamId id) {
	return patched_short(id).value_or(std::string_view{""});
}

std::string_view patched_param_short_name(plugin::ParamId id, effects::FilterMode lpf, effects::FilterMode hpf) {
	if (id == instruments::classic::param::kLpfMorph && is_family(lpf, effects::FilterFamily::LP_LADDER)) {
		return "LPF Drive";
	}
	if (id == instruments::classic::param::kHpfMorph && is_family(hpf, effects::FilterFamily::HP_LADDER)) {
		return "HPF FM";
	}
	return patched_short(id).value_or(std::string_view{""});
}

std::string_view patched_param_short_name_at(uint32_t index) {
	return patched_param_short_name(instruments::classic::flat_param_id(index));
}

std::string_view patched_param_short_name_at(uint32_t index, effects::FilterMode lpf, effects::FilterMode hpf) {
	return patched_param_short_name(instruments::classic::flat_param_id(index), lpf, hpf);
}

std::optional<std::string_view> envelope_param_name(plugin::ParamId id, const EnvelopeModes& modes, bool brief) {
	if (!instruments::classic::is_patched(id)) {
		return std::nullopt;
	}
	// The four envelopes' copies of a param are consecutive
	constexpr uint32_t kDecay = instruments::classic::param::kEnv1Decay.value;
	constexpr uint32_t kSustain = instruments::classic::param::kEnv1Sustain.value;
	constexpr uint32_t kRelease = instruments::classic::param::kEnv1Release.value;
	const auto envelope_of = [index = id.value](uint32_t first) -> std::optional<uint32_t> {
		return index >= first && index - first < 4 ? std::optional{index - first} : std::nullopt;
	};
	static constexpr std::array<std::string_view, 4> kHold{"Env1 hold", "Env2 hold", "Env3 hold", "Env4 hold"};
	static constexpr std::array<std::string_view, 4> kLevel{"Env1 level", "Env2 level", "Env3 level", "Env4 level"};
	if (const auto e = envelope_of(kDecay); e && modes[*e] == synth::EnvelopeMode::AHD) {
		return kHold[*e];
	}
	if (const auto e = envelope_of(kRelease); e && modes[*e] == synth::EnvelopeMode::AHD) {
		const plugin::ParamId decay{kDecay + *e};
		return brief ? patched_param_short_name(decay) : param_name(project::OwnerPlugin::INSTRUMENT, decay);
	}
	if (const auto e = envelope_of(kSustain);
	    e && (modes[*e] == synth::EnvelopeMode::AR || modes[*e] == synth::EnvelopeMode::MPE)) {
		return kLevel[*e];
	}
	return std::nullopt;
}

std::optional<std::string_view> envelope_param_name_at(plugin::ParamKind kind, uint32_t index,
                                                       const EnvelopeModes& modes, bool brief) {
	if (kind != plugin::ParamKind::PATCHED) {
		return std::nullopt;
	}
	return envelope_param_name(instruments::classic::flat_param_id(index), modes, brief);
}

} // namespace deluge::l10n
