/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "modulation/params/param.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/song/song.h"
#include "util/container/enum_to_string_map.hpp"
#include <cstring>

namespace deluge::modulation::params {

bool isParamBipolar(Kind kind, int32_t paramID) {
	return (kind == Kind::PATCH_CABLE) || isParamPan(kind, paramID) || isParamPitch(kind, paramID)
	       || isParamPitchBend(kind, paramID);
}

bool isParamPan(Kind kind, int32_t paramID) {
	return (kind == Kind::PATCHED && paramID == LOCAL_PAN)
	       || (kind == Kind::UNPATCHED_GLOBAL && paramID == UNPATCHED_PAN);
}

bool isParamArpRhythm(Kind kind, int32_t paramID) {
	return (kind == Kind::UNPATCHED_SOUND && paramID == UNPATCHED_ARP_RHYTHM);
}

bool isParamPitch(Kind kind, int32_t paramID) {
	if (kind == Kind::PATCHED) {
		return (paramID == LOCAL_PITCH_ADJUST) || (paramID == LOCAL_OSC_A_PITCH_ADJUST)
		       || (paramID == LOCAL_OSC_B_PITCH_ADJUST) || (paramID == LOCAL_MODULATOR_0_PITCH_ADJUST)
		       || (paramID == LOCAL_MODULATOR_1_PITCH_ADJUST);
	}
	else if (kind == Kind::UNPATCHED_GLOBAL) {
		return static_cast<UnpatchedGlobal>(paramID) == UNPATCHED_PITCH_ADJUST;
	}
	else {
		return false;
	}
}

bool isParamPitchBend(Kind kind, int32_t paramID) {
	return (kind == Kind::EXPRESSION && paramID == Expression::X_PITCH_BEND);
}

bool isParamStutter(Kind kind, int32_t paramID) {
	return (kind == Kind::UNPATCHED_GLOBAL || kind == Kind::UNPATCHED_SOUND)
	       && static_cast<UnpatchedShared>(paramID) == UNPATCHED_STUTTER_RATE;
}

bool isParamQuantizedStutter(Kind kind, int32_t paramID, ModControllableAudio* modControllableAudio) {
	return (kind == Kind::UNPATCHED_GLOBAL || kind == Kind::UNPATCHED_SOUND)
	       && static_cast<UnpatchedShared>(paramID) == UNPATCHED_STUTTER_RATE && modControllableAudio
	       && (modControllableAudio->stutterConfig.useSongStutter
	               ? currentSong->globalEffectable.stutterConfig.quantized
	               : modControllableAudio->stutterConfig.quantized);
}

bool isVibratoPatchCableShortcut(int32_t xDisplay, int32_t yDisplay) {
	if (xDisplay == 6 && yDisplay == 2) {
		return true;
	}
	return false;
}

bool isSidechainPatchCableShortcut(int32_t xDisplay, int32_t yDisplay) {
	if (xDisplay == 10 && yDisplay == 2) {
		return true;
	}
	return false;
}

bool isPatchCableShortcut(int32_t xDisplay, int32_t yDisplay) {
	// vibrato shortcut
	if (isVibratoPatchCableShortcut(xDisplay, yDisplay)) {
		return true;
	}
	// sidechain volume ducking shortcut
	else if (isSidechainPatchCableShortcut(xDisplay, yDisplay)) {
		return true;
	}
	return false;
}

void getPatchCableFromShortcut(int32_t xDisplay, int32_t yDisplay, ParamDescriptor* paramDescriptor) {
	// vibrato shortcut
	if (isVibratoPatchCableShortcut(xDisplay, yDisplay)) {
		paramDescriptor->setToHaveParamAndSource(LOCAL_PITCH_ADJUST, PatchSource::LFO_GLOBAL_1);
	}
	// sidechain volume ducking shortcut
	else if (isSidechainPatchCableShortcut(xDisplay, yDisplay)) {
		paramDescriptor->setToHaveParamAndSource(GLOBAL_VOLUME_POST_REVERB_SEND, PatchSource::SIDECHAIN);
	}
}

char const* getPatchedParamShortName(ParamType type) {
	// clang-format off
	static char const* const NAMES[GLOBAL_NONE] = {
	                                      //          //
	    [LOCAL_OSC_A_VOLUME]             = "Osc1 level",
	    [LOCAL_OSC_B_VOLUME]             = "Osc2 level",
	    [LOCAL_VOLUME]                   = "Level",
	    [LOCAL_NOISE_VOLUME]             = "Noise",
	    [LOCAL_MODULATOR_0_VOLUME]       = "Mod1 level",
	    [LOCAL_MODULATOR_1_VOLUME]       = "Mod2 level",
	    [LOCAL_FOLD]                     = "Wavefold",
	    [LOCAL_MODULATOR_0_FEEDBACK]     = "Mod1 feed",
	    [LOCAL_MODULATOR_1_FEEDBACK]     = "Mod2 feed",
	    [LOCAL_CARRIER_0_FEEDBACK]       = "Osc1 feed",
	    [LOCAL_CARRIER_1_FEEDBACK]       = "Osc2 feed",
	    [LOCAL_LPF_RESONANCE]            = "LPF reso",
	    [LOCAL_HPF_RESONANCE]            = "HPF reso",
	    [LOCAL_ENV_0_SUSTAIN]            = "Env1 sus",
	    [LOCAL_ENV_1_SUSTAIN]            = "Env2 sus",
	    [LOCAL_ENV_2_SUSTAIN]            = "Env3 sus",
	    [LOCAL_ENV_3_SUSTAIN]            = "Env4 sus",
	    [LOCAL_LPF_MORPH]                = "LPF Morph",
	    [LOCAL_HPF_MORPH]                = "HPF Morph",
	    [LOCAL_OSC_A_PHASE_WIDTH]        = "Osc1 PW",
	    [LOCAL_OSC_B_PHASE_WIDTH]        = "Osc2 PW",
	    [LOCAL_OSC_A_WAVE_INDEX]         = "Osc1 wave",
	    [LOCAL_OSC_B_WAVE_INDEX]         = "Osc2 wave",
	    [LOCAL_PAN]                      = "Pan",
	    [LOCAL_TABLE_SHAPER_DRIVE]       = "Shpr drive",
	    [LOCAL_SINE_SHAPER_DRIVE]        = "Sine drive",
	    [LOCAL_TABLE_SHAPER_MIX]         = "Shaper mix",
	    [LOCAL_SINE_SHAPER_TWIST]        = "Sine twist",
	    [LOCAL_SINE_SHAPER_HARMONIC]     = "Sine harm",
	    [LOCAL_LPF_FREQ]                 = "LPf freq",
	    [LOCAL_PITCH_ADJUST]             = "Pitch",
	    [LOCAL_OSC_A_PITCH_ADJUST]       = "Osc1 pitch",
	    [LOCAL_OSC_B_PITCH_ADJUST]       = "Osc2 pitch",
	    [LOCAL_MODULATOR_0_PITCH_ADJUST] = "Mod1 pitch",
	    [LOCAL_MODULATOR_1_PITCH_ADJUST] = "Mod1 pitch",
	    [LOCAL_HPF_FREQ]                 = "HPF freq",
	    [LOCAL_LFO_LOCAL_FREQ_1]           = "LFO2 rate",
	    [LOCAL_LFO_LOCAL_FREQ_2]           = "LFO4 rate",
	    [LOCAL_ENV_0_ATTACK]             = "Env1attack",
	    [LOCAL_ENV_1_ATTACK]             = "Env2attack",
	    [LOCAL_ENV_2_ATTACK]             = "Env3attack",
	    [LOCAL_ENV_3_ATTACK]             = "Env4attack",
	    [LOCAL_ENV_0_DECAY]              = "Env1 decay",
	    [LOCAL_ENV_1_DECAY]              = "Env2 decay",
	    [LOCAL_ENV_2_DECAY]              = "Env3 decay",
	    [LOCAL_ENV_3_DECAY]              = "Env4 decay",
	    [LOCAL_ENV_0_RELEASE]            = "Env1 rel",
	    [LOCAL_ENV_1_RELEASE]            = "Env2 rel",
	    [LOCAL_ENV_2_RELEASE]            = "Env3 rel",
	    [LOCAL_ENV_3_RELEASE]            = "Env4 rel",
	    [GLOBAL_VOLUME_POST_FX]          = "POSTFXLVL",
	    [GLOBAL_VOLUME_POST_REVERB_SEND] = "Side level",
	    [GLOBAL_REVERB_AMOUNT]           = "Reverb amt",
	    [GLOBAL_MOD_FX_DEPTH]            = "ModFXdepth",
	    [GLOBAL_DELAY_FEEDBACK]          = "Delay feed",
	    [GLOBAL_SCATTER_MACRO]           = "Scat macro",
	    [GLOBAL_SCATTER_PWRITE]          = "Scat pWrite",
	    [GLOBAL_SCATTER_DENSITY]         = "Scat density",
	    [GLOBAL_SCATTER_ZONE_A]          = "Scat zoneA",
	    [GLOBAL_SCATTER_ZONE_B]          = "Scat zoneB",
	    [GLOBAL_SCATTER_MACRO_CONFIG]    = "Scat depth",
	    [GLOBAL_AUTOMOD_DEPTH]           = "Automod",
	    [GLOBAL_AUTOMOD_FREQ]            = "AutoFrq",
	    [GLOBAL_AUTOMOD_MANUAL]          = "AutoMan",
	    [GLOBAL_DELAY_RATE]              = "Delay rate",
	    [GLOBAL_MOD_FX_RATE]             = "ModFX rate",
	    [GLOBAL_LFO_FREQ_1]                = "LFO1 rate",
	    [GLOBAL_LFO_FREQ_2]                = "LFO3 rate",
	    [GLOBAL_ARP_RATE]                = "Arp. rate",
	};
	// clang-format on

	if (type < GLOBAL_NONE) {
		return NAMES[type];
	}
	else {
		__builtin_unreachable();
		return nullptr;
	}
}

char const* getPatchedParamDisplayName(int32_t p) {
	using enum l10n::String;

	static l10n::String const NAMES[GLOBAL_NONE] = {
	    [LOCAL_OSC_A_VOLUME] = STRING_FOR_PARAM_LOCAL_OSC_A_VOLUME,
	    [LOCAL_OSC_B_VOLUME] = STRING_FOR_PARAM_LOCAL_OSC_B_VOLUME,
	    [LOCAL_VOLUME] = STRING_FOR_PARAM_LOCAL_VOLUME,
	    [LOCAL_NOISE_VOLUME] = STRING_FOR_PARAM_LOCAL_NOISE_VOLUME,
	    [LOCAL_MODULATOR_0_VOLUME] = STRING_FOR_PARAM_LOCAL_MODULATOR_0_VOLUME,
	    [LOCAL_MODULATOR_1_VOLUME] = STRING_FOR_PARAM_LOCAL_MODULATOR_1_VOLUME,
	    [LOCAL_FOLD] = STRING_FOR_WAVEFOLDER,
	    [LOCAL_MODULATOR_0_FEEDBACK] = STRING_FOR_PARAM_LOCAL_MODULATOR_0_FEEDBACK,
	    [LOCAL_MODULATOR_1_FEEDBACK] = STRING_FOR_PARAM_LOCAL_MODULATOR_1_FEEDBACK,
	    [LOCAL_CARRIER_0_FEEDBACK] = STRING_FOR_PARAM_LOCAL_CARRIER_0_FEEDBACK,
	    [LOCAL_CARRIER_1_FEEDBACK] = STRING_FOR_PARAM_LOCAL_CARRIER_1_FEEDBACK,
	    [LOCAL_LPF_RESONANCE] = STRING_FOR_PARAM_LOCAL_LPF_RESONANCE,
	    [LOCAL_HPF_RESONANCE] = STRING_FOR_PARAM_LOCAL_HPF_RESONANCE,
	    [LOCAL_ENV_0_SUSTAIN] = STRING_FOR_PARAM_LOCAL_ENV_0_SUSTAIN,
	    [LOCAL_ENV_1_SUSTAIN] = STRING_FOR_PARAM_LOCAL_ENV_1_SUSTAIN,
	    [LOCAL_ENV_2_SUSTAIN] = STRING_FOR_PARAM_LOCAL_ENV_2_SUSTAIN,
	    [LOCAL_ENV_3_SUSTAIN] = STRING_FOR_PARAM_LOCAL_ENV_3_SUSTAIN,
	    [LOCAL_LPF_MORPH] = STRING_FOR_PARAM_LOCAL_LPF_MORPH,
	    [LOCAL_HPF_MORPH] = STRING_FOR_PARAM_LOCAL_HPF_MORPH,
	    [LOCAL_OSC_A_PHASE_WIDTH] = STRING_FOR_PARAM_LOCAL_OSC_A_PHASE_WIDTH,
	    [LOCAL_OSC_B_PHASE_WIDTH] = STRING_FOR_PARAM_LOCAL_OSC_B_PHASE_WIDTH,
	    [LOCAL_OSC_A_WAVE_INDEX] = STRING_FOR_PARAM_LOCAL_OSC_A_WAVE_INDEX,
	    [LOCAL_OSC_B_WAVE_INDEX] = STRING_FOR_PARAM_LOCAL_OSC_B_WAVE_INDEX,
	    [LOCAL_PAN] = STRING_FOR_PARAM_LOCAL_PAN,
	    [LOCAL_TABLE_SHAPER_DRIVE] = STRING_FOR_PARAM_LOCAL_TABLE_SHAPER_DRIVE,
	    [LOCAL_SINE_SHAPER_DRIVE] = STRING_FOR_PARAM_LOCAL_SINE_SHAPER_DRIVE,
	    [LOCAL_TABLE_SHAPER_MIX] = STRING_FOR_PARAM_LOCAL_TABLE_SHAPER_MIX,
	    [LOCAL_SINE_SHAPER_TWIST] = STRING_FOR_SINE_SHAPER_SYMMETRY, // Reuse existing twist/symmetry string
	    [LOCAL_SINE_SHAPER_HARMONIC] = STRING_FOR_SINE_SHAPER_HARMONIC,
	    [LOCAL_LPF_FREQ] = STRING_FOR_PARAM_LOCAL_LPF_FREQ,
	    [LOCAL_PITCH_ADJUST] = STRING_FOR_PARAM_LOCAL_PITCH_ADJUST,
	    [LOCAL_OSC_A_PITCH_ADJUST] = STRING_FOR_PARAM_LOCAL_OSC_A_PITCH_ADJUST,
	    [LOCAL_OSC_B_PITCH_ADJUST] = STRING_FOR_PARAM_LOCAL_OSC_B_PITCH_ADJUST,
	    [LOCAL_MODULATOR_0_PITCH_ADJUST] = STRING_FOR_PARAM_LOCAL_MODULATOR_0_PITCH_ADJUST,
	    [LOCAL_MODULATOR_1_PITCH_ADJUST] = STRING_FOR_PARAM_LOCAL_MODULATOR_1_PITCH_ADJUST,
	    [LOCAL_HPF_FREQ] = STRING_FOR_PARAM_LOCAL_HPF_FREQ,
	    [LOCAL_LFO_LOCAL_FREQ_1] = STRING_FOR_PARAM_LOCAL_LFO_LOCAL_FREQ_1,
	    [LOCAL_LFO_LOCAL_FREQ_2] = STRING_FOR_PARAM_LOCAL_LFO_LOCAL_FREQ_2,
	    [LOCAL_ENV_0_ATTACK] = STRING_FOR_PARAM_LOCAL_ENV_0_ATTACK,
	    [LOCAL_ENV_1_ATTACK] = STRING_FOR_PARAM_LOCAL_ENV_1_ATTACK,
	    [LOCAL_ENV_2_ATTACK] = STRING_FOR_PARAM_LOCAL_ENV_2_ATTACK,
	    [LOCAL_ENV_3_ATTACK] = STRING_FOR_PARAM_LOCAL_ENV_3_ATTACK,
	    [LOCAL_ENV_0_DECAY] = STRING_FOR_PARAM_LOCAL_ENV_0_DECAY,
	    [LOCAL_ENV_1_DECAY] = STRING_FOR_PARAM_LOCAL_ENV_1_DECAY,
	    [LOCAL_ENV_2_DECAY] = STRING_FOR_PARAM_LOCAL_ENV_2_DECAY,
	    [LOCAL_ENV_3_DECAY] = STRING_FOR_PARAM_LOCAL_ENV_3_DECAY,
	    [LOCAL_ENV_0_RELEASE] = STRING_FOR_PARAM_LOCAL_ENV_0_RELEASE,
	    [LOCAL_ENV_1_RELEASE] = STRING_FOR_PARAM_LOCAL_ENV_1_RELEASE,
	    [LOCAL_ENV_2_RELEASE] = STRING_FOR_PARAM_LOCAL_ENV_2_RELEASE,
	    [LOCAL_ENV_3_RELEASE] = STRING_FOR_PARAM_LOCAL_ENV_3_RELEASE,
	    [GLOBAL_VOLUME_POST_FX] = STRING_FOR_PARAM_GLOBAL_VOLUME_POST_FX,
	    [GLOBAL_VOLUME_POST_REVERB_SEND] = STRING_FOR_PARAM_GLOBAL_VOLUME_POST_REVERB_SEND,
	    [GLOBAL_REVERB_AMOUNT] = STRING_FOR_PARAM_GLOBAL_REVERB_AMOUNT,
	    [GLOBAL_MOD_FX_DEPTH] = STRING_FOR_PARAM_GLOBAL_MOD_FX_DEPTH,
	    [GLOBAL_DELAY_FEEDBACK] = STRING_FOR_PARAM_GLOBAL_DELAY_FEEDBACK,
	    [GLOBAL_SCATTER_MACRO] = STRING_FOR_SCATTER_MACRO,
	    [GLOBAL_SCATTER_PWRITE] = STRING_FOR_SCATTER_PWRITE,
	    [GLOBAL_SCATTER_DENSITY] = STRING_FOR_SCATTER_DENSITY,
	    [GLOBAL_SCATTER_ZONE_A] = STRING_FOR_SCATTER_PATTERN,
	    [GLOBAL_SCATTER_ZONE_B] = STRING_FOR_SCATTER_COLOR,
	    [GLOBAL_SCATTER_MACRO_CONFIG] = STRING_FOR_SCATTER_MACRO_CONFIG,
	    [GLOBAL_AUTOMOD_DEPTH] = STRING_FOR_AUTOMOD_DEPTH,
	    [GLOBAL_AUTOMOD_FREQ] = STRING_FOR_AUTOMOD_FREQ,
	    [GLOBAL_AUTOMOD_MANUAL] = STRING_FOR_AUTOMOD_MANUAL,
	    [GLOBAL_DELAY_RATE] = STRING_FOR_PARAM_GLOBAL_DELAY_RATE,
	    [GLOBAL_MOD_FX_RATE] = STRING_FOR_PARAM_GLOBAL_MOD_FX_RATE,
	    [GLOBAL_LFO_FREQ_1] = STRING_FOR_PARAM_GLOBAL_LFO_FREQ_1,
	    [GLOBAL_LFO_FREQ_2] = STRING_FOR_PARAM_GLOBAL_LFO_FREQ_2,
	    [GLOBAL_ARP_RATE] = STRING_FOR_PARAM_GLOBAL_ARP_RATE,
	};

	if (p < GLOBAL_NONE) {
		// These can basically be 13 chars long, or 14 if the last one is a dot.
		return l10n::get(NAMES[p]);
	}
	else {
		return l10n::get(STRING_FOR_NONE);
	}
}

char const* getParamDisplayName(Kind kind, int32_t p) {
	using enum l10n::String;
	if (kind == Kind::PATCHED) {
		return getPatchedParamDisplayName(p);
	}

	if ((kind == Kind::UNPATCHED_SOUND || kind == Kind::UNPATCHED_GLOBAL) && p < (UNPATCHED_NUM_SHARED)) {
		static l10n::String const NAMES[UNPATCHED_NUM_SHARED] = {
		    [UNPATCHED_STUTTER_RATE] = STRING_FOR_STUTTER_RATE,
		    [UNPATCHED_BASS] = STRING_FOR_BASS,
		    [UNPATCHED_TREBLE] = STRING_FOR_TREBLE,
		    [UNPATCHED_BASS_FREQ] = STRING_FOR_BASS_FREQUENCY,
		    [UNPATCHED_TREBLE_FREQ] = STRING_FOR_TREBLE_FREQUENCY,
		    [UNPATCHED_SAMPLE_RATE_REDUCTION] = STRING_FOR_DECIMATION,
		    [UNPATCHED_BITCRUSHING] = STRING_FOR_BITCRUSH,
		    [UNPATCHED_MOD_FX_OFFSET] = STRING_FOR_MODFX_OFFSET,
		    [UNPATCHED_MOD_FX_FEEDBACK] = STRING_FOR_MODFX_FEEDBACK,
		    [UNPATCHED_SIDECHAIN_SHAPE] = STRING_FOR_SIDECHAIN_SHAPE,
		    [UNPATCHED_COMPRESSOR_THRESHOLD] = STRING_FOR_THRESHOLD,
		    [UNPATCHED_MB_COMPRESSOR_CHARACTER] = STRING_FOR_COMPRESSOR_CHARACTER,
		    [UNPATCHED_MB_COMPRESSOR_LOW_CROSSOVER] = STRING_FOR_COMPRESSOR_LOW_CROSSOVER,
		    [UNPATCHED_MB_COMPRESSOR_HIGH_CROSSOVER] = STRING_FOR_COMPRESSOR_HIGH_CROSSOVER,
		    [UNPATCHED_MB_COMPRESSOR_THRESHOLD] = STRING_FOR_THRESHOLD,
		    [UNPATCHED_MB_COMPRESSOR_RATIO] = STRING_FOR_RATIO,
		    [UNPATCHED_MB_COMPRESSOR_ATTACK] = STRING_FOR_ATTACK,
		    [UNPATCHED_MB_COMPRESSOR_RELEASE] = STRING_FOR_RELEASE,
		    [UNPATCHED_MB_COMPRESSOR_SKEW] = STRING_FOR_COMPRESSOR_UP_DOWN_SKEW,
		    [UNPATCHED_MB_COMPRESSOR_LOW_LEVEL] = STRING_FOR_COMPRESSOR_LOW_LEVEL,
		    [UNPATCHED_MB_COMPRESSOR_MID_LEVEL] = STRING_FOR_COMPRESSOR_MID_LEVEL,
		    [UNPATCHED_MB_COMPRESSOR_HIGH_LEVEL] = STRING_FOR_COMPRESSOR_HIGH_LEVEL,
		    [UNPATCHED_MB_COMPRESSOR_OUTPUT_GAIN] = STRING_FOR_COMPRESSOR_OUTPUT_GAIN,
		    [UNPATCHED_MB_COMPRESSOR_VIBE] = STRING_FOR_COMPRESSOR_VIBE,
		    [UNPATCHED_MB_COMPRESSOR_BLEND] = STRING_FOR_BLEND,
		    [UNPATCHED_SINE_SHAPER_DRIVE] = STRING_FOR_SINE_SHAPER_DRIVE,
		    [UNPATCHED_SINE_SHAPER_HARMONIC] = STRING_FOR_SINE_SHAPER_HARMONIC,
		    [UNPATCHED_SINE_SHAPER_TWIST] = STRING_FOR_SINE_SHAPER_SYMMETRY,
		    [UNPATCHED_TABLE_SHAPER_DRIVE] = STRING_FOR_SHAPER_DRIVE,
		    [UNPATCHED_TABLE_SHAPER_MIX] = STRING_FOR_SHAPER_MIX,
		    [UNPATCHED_AUTOMOD_DEPTH] = STRING_FOR_AUTOMOD_DEPTH,
		    [UNPATCHED_AUTOMOD_FREQ] = STRING_FOR_AUTOMOD_FREQ,
		    [UNPATCHED_AUTOMOD_MANUAL] = STRING_FOR_AUTOMOD_MANUAL,
		    [UNPATCHED_SCATTER_ZONE_A] = STRING_FOR_SCATTER_PATTERN,
		    [UNPATCHED_SCATTER_ZONE_B] = STRING_FOR_SCATTER_COLOR,
		    [UNPATCHED_SCATTER_MACRO_CONFIG] = STRING_FOR_SCATTER_MACRO_CONFIG,
		    [UNPATCHED_SCATTER_MACRO] = STRING_FOR_SCATTER_MACRO,
		    [UNPATCHED_SCATTER_PWRITE] = STRING_FOR_SCATTER_PWRITE,
		    [UNPATCHED_SCATTER_DENSITY] = STRING_FOR_SCATTER_DENSITY,
		    [UNPATCHED_ARP_GATE] = STRING_FOR_ARP_GATE_MENU_TITLE,
		    [UNPATCHED_ARP_RHYTHM] = STRING_FOR_ARP_RHYTHM_MENU_TITLE,
		    [UNPATCHED_ARP_SEQUENCE_LENGTH] = STRING_FOR_ARP_SEQUENCE_LENGTH_MENU_TITLE,
		    [UNPATCHED_ARP_CHORD_POLYPHONY] = STRING_FOR_ARP_CHORD_POLYPHONY_MENU_TITLE,
		    [UNPATCHED_ARP_RATCHET_AMOUNT] = STRING_FOR_ARP_RATCHETS_MENU_TITLE,
		    [UNPATCHED_NOTE_PROBABILITY] = STRING_FOR_NOTE_PROBABILITY_MENU_TITLE,
		    [UNPATCHED_REVERSE_PROBABILITY] = STRING_FOR_REVERSE_PROBABILITY_MENU_TITLE,
		    [UNPATCHED_ARP_BASS_PROBABILITY] = STRING_FOR_ARP_BASS_PROBABILITY_MENU_TITLE,
		    [UNPATCHED_ARP_SWAP_PROBABILITY] = STRING_FOR_ARP_SWAP_PROBABILITY_MENU_TITLE,
		    [UNPATCHED_ARP_GLIDE_PROBABILITY] = STRING_FOR_ARP_GLIDE_PROBABILITY_MENU_TITLE,
		    [UNPATCHED_ARP_CHORD_PROBABILITY] = STRING_FOR_ARP_CHORD_PROBABILITY_MENU_TITLE,
		    [UNPATCHED_ARP_RATCHET_PROBABILITY] = STRING_FOR_ARP_RATCHET_PROBABILITY_MENU_TITLE,
		    [UNPATCHED_ARP_SPREAD_GATE] = STRING_FOR_ARP_SPREAD_GATE_MENU_TITLE,
		    [UNPATCHED_ARP_SPREAD_OCTAVE] = STRING_FOR_ARP_SPREAD_OCTAVE_MENU_TITLE,
		    [UNPATCHED_SPREAD_VELOCITY] = STRING_FOR_SPREAD_VELOCITY_MENU_TITLE,
		};
		return l10n::get(NAMES[p]);
	}

	if (kind == Kind::EXPRESSION && p < kNumExpressionDimensions) {
		static l10n::String const NAMES[kNumExpressionDimensions] = {
		    [Expression::X_PITCH_BEND] = STRING_FOR_PITCH_BEND,
		    [Expression::Y_SLIDE_TIMBRE] = STRING_FOR_MOD_WHEEL,
		    [Expression::Z_PRESSURE] = STRING_FOR_CHANNEL_PRESSURE,
		};
		return l10n::get(NAMES[p]);
	}

	constexpr ParamType unc = UNPATCHED_NUM_SHARED;

	if (kind == Kind::UNPATCHED_SOUND && p < util::to_underlying(UNPATCHED_SOUND_MAX_NUM)) {
		using enum UnpatchedSound;
		static l10n::String const NAMES[UNPATCHED_SOUND_MAX_NUM - unc] = {
		    [UNPATCHED_PORTAMENTO - unc] = STRING_FOR_PORTAMENTO,
		};
		return l10n::get(NAMES[p - unc]);
	}

	if (kind == Kind::UNPATCHED_GLOBAL && p < UNPATCHED_GLOBAL_MAX_NUM) {
		using enum UnpatchedGlobal;

		static l10n::String const NAMES[UNPATCHED_GLOBAL_MAX_NUM - unc] = {
		    [UNPATCHED_MOD_FX_RATE - unc] = STRING_FOR_MOD_FX_RATE,
		    [UNPATCHED_MOD_FX_DEPTH - unc] = STRING_FOR_MOD_FX_DEPTH,
		    [UNPATCHED_DELAY_RATE - unc] = STRING_FOR_DELAY_RATE,
		    [UNPATCHED_DELAY_AMOUNT - unc] = STRING_FOR_DELAY_AMOUNT,
		    [UNPATCHED_ARP_RATE - unc] = STRING_FOR_ARP_RATE_MENU_TITLE,
		    [UNPATCHED_PAN - unc] = STRING_FOR_PAN,
		    [UNPATCHED_LPF_FREQ - unc] = STRING_FOR_LPF_FREQUENCY,
		    [UNPATCHED_LPF_RES - unc] = STRING_FOR_LPF_RESONANCE,
		    [UNPATCHED_LPF_MORPH - unc] = STRING_FOR_LPF_MORPH,
		    [UNPATCHED_HPF_FREQ - unc] = STRING_FOR_HPF_FREQUENCY,
		    [UNPATCHED_HPF_RES - unc] = STRING_FOR_HPF_RESONANCE,
		    [UNPATCHED_HPF_MORPH - unc] = STRING_FOR_HPF_MORPH,
		    [UNPATCHED_REVERB_SEND_AMOUNT - unc] = STRING_FOR_REVERB_AMOUNT,
		    [UNPATCHED_VOLUME - unc] = STRING_FOR_MASTER_LEVEL,
		    [UNPATCHED_SIDECHAIN_VOLUME - unc] = STRING_FOR_SIDECHAIN_LEVEL,
		    [UNPATCHED_PITCH_ADJUST - unc] = STRING_FOR_MASTER_PITCH,
		    [UNPATCHED_TEMPO - unc] = STRING_FOR_TEMPO,
		};
		return l10n::get(NAMES[p - unc]);
	}

	return l10n::get(STRING_FOR_NONE);
}

bool paramNeedsLPF(ParamType p, bool fromAutomation) {
	switch (p) {

	// For many params, particularly volumes, we do want the param LPF if the user adjusted it,
	// so we don't get stepping, but if it's from step automation, we do want it to adjust instantly,
	// so the new step is instantly at the right volume
	case params::GLOBAL_VOLUME_POST_FX:
	case params::GLOBAL_VOLUME_POST_REVERB_SEND:
	case params::GLOBAL_REVERB_AMOUNT:
	case params::LOCAL_VOLUME:
	case params::LOCAL_PAN:
	case params::LOCAL_LPF_FREQ:
	case params::LOCAL_HPF_FREQ:
	case params::LOCAL_OSC_A_VOLUME:
	case params::LOCAL_OSC_B_VOLUME:
	case params::LOCAL_OSC_A_WAVE_INDEX:
	case params::LOCAL_OSC_B_WAVE_INDEX:
		return !fromAutomation;

	case params::LOCAL_MODULATOR_0_VOLUME:
	case params::LOCAL_MODULATOR_1_VOLUME:
	case params::LOCAL_MODULATOR_0_FEEDBACK:
	case params::LOCAL_MODULATOR_1_FEEDBACK:
	case params::LOCAL_CARRIER_0_FEEDBACK:
	case params::LOCAL_CARRIER_1_FEEDBACK:
	case params::GLOBAL_MOD_FX_DEPTH:
	case params::GLOBAL_DELAY_FEEDBACK:
		return true;

	default:
		return false;
	}
}

constexpr char const* paramNameForFileConst(Kind const kind, ParamType const param) {
	using enum Kind;
	if (kind == UNPATCHED_SOUND && param >= UNPATCHED_START + UNPATCHED_NUM_SHARED) {
		// Unpatched params just for Sounds
		switch (static_cast<UnpatchedSound>(param - UNPATCHED_START)) {
		case UNPATCHED_PORTAMENTO:
			return "portamento";

		default:
		    // Fall through to the other param kind handling
		    ;
		}
	}
	else if (kind == UNPATCHED_GLOBAL && param >= UNPATCHED_START + UNPATCHED_NUM_SHARED) {
		// Params for GlobalEffectable
		switch (static_cast<UnpatchedGlobal>(param - UNPATCHED_START)) {
		case UNPATCHED_MOD_FX_RATE:
			return "modFXRate";

		case UNPATCHED_MOD_FX_DEPTH:
			return "modFXDepth";

		case UNPATCHED_DELAY_RATE:
			return "delayRate";

		case UNPATCHED_DELAY_AMOUNT:
			return "delayFeedback";

		case UNPATCHED_ARP_RATE:
			return "arpRate";

		case UNPATCHED_PAN:
			return "pan";

		case UNPATCHED_LPF_FREQ:
			return "lpfFrequency";
		case UNPATCHED_LPF_RES:
			return "lpfResonance";
		case UNPATCHED_LPF_MORPH:
			return "lpfMorph";

		case UNPATCHED_HPF_FREQ:
			return "hpfFrequency";
		case UNPATCHED_HPF_MORPH:
			return "hpfMorph";
		case UNPATCHED_HPF_RES:
			return "hpfResonance";

		case UNPATCHED_REVERB_SEND_AMOUNT:
			return "reverbAmount";

		case UNPATCHED_VOLUME:
			return "volume";

		case UNPATCHED_SIDECHAIN_VOLUME:
			return "sidechainCompressorVolume";

		case UNPATCHED_PITCH_ADJUST:
			return "pitchAdjust";

		// explicit fallthrough cases
		case UNPATCHED_TEMPO: // nothing, really?
		case UNPATCHED_GLOBAL_MAX_NUM:;
		}
	}

	else if (param >= UNPATCHED_START) {
		switch (static_cast<UnpatchedShared>(param - UNPATCHED_START)) {
		case UNPATCHED_STUTTER_RATE:
			return "stutterRate";

		case UNPATCHED_BASS:
			return "bass";

		case UNPATCHED_TREBLE:
			return "treble";

		case UNPATCHED_BASS_FREQ:
			return "bassFreq";

		case UNPATCHED_TREBLE_FREQ:
			return "trebleFreq";

		case UNPATCHED_SAMPLE_RATE_REDUCTION:
			return "sampleRateReduction";

		case UNPATCHED_BITCRUSHING:
			return "bitcrushAmount";

		case UNPATCHED_MOD_FX_OFFSET:
			return "modFXOffset";

		case UNPATCHED_MOD_FX_FEEDBACK:
			return "modFXFeedback";

		case UNPATCHED_SIDECHAIN_SHAPE:
			return "compressorShape";

		case UNPATCHED_COMPRESSOR_THRESHOLD:
			return "compressorThreshold";

		case UNPATCHED_MB_COMPRESSOR_CHARACTER:
			return "mbCompressorCharacter";
		case UNPATCHED_MB_COMPRESSOR_LOW_CROSSOVER:
			return "mbCompressorLowCrossover";
		case UNPATCHED_MB_COMPRESSOR_HIGH_CROSSOVER:
			return "mbCompressorHighCrossover";
		case UNPATCHED_MB_COMPRESSOR_THRESHOLD:
			return "mbCompressorThreshold";
		case UNPATCHED_MB_COMPRESSOR_RATIO:
			return "mbCompressorRatio";
		case UNPATCHED_MB_COMPRESSOR_ATTACK:
			return "mbCompressorAttack";
		case UNPATCHED_MB_COMPRESSOR_RELEASE:
			return "mbCompressorRelease";
		case UNPATCHED_MB_COMPRESSOR_SKEW:
			return "mbCompressorSkew";
		case UNPATCHED_MB_COMPRESSOR_LOW_LEVEL:
			return "mbCompressorLowLevel";
		case UNPATCHED_MB_COMPRESSOR_MID_LEVEL:
			return "mbCompressorMidLevel";
		case UNPATCHED_MB_COMPRESSOR_HIGH_LEVEL:
			return "mbCompressorHighLevel";
		case UNPATCHED_MB_COMPRESSOR_OUTPUT_GAIN:
			return "mbCompressorOutputGain";
		case UNPATCHED_MB_COMPRESSOR_VIBE:
			return "mbCompressorVibe";
		case UNPATCHED_MB_COMPRESSOR_BLEND:
			return "mbCompressorBlend";

		// Shapers (prefixed to avoid conflicts with LOCAL_* patched versions)
		case UNPATCHED_SINE_SHAPER_DRIVE:
			return "clipSineShaperDrive";
		case UNPATCHED_SINE_SHAPER_HARMONIC:
			return "clipSineShaperHarmonic";
		case UNPATCHED_SINE_SHAPER_TWIST:
			return "clipSineShaperSymmetry";
		case UNPATCHED_TABLE_SHAPER_DRIVE:
			return "clipTableShaperDrive";
		case UNPATCHED_TABLE_SHAPER_MIX:
			return "clipTableShaperMix";
		case UNPATCHED_AUTOMOD_DEPTH:
			return "clipAutomodMacro";
		case UNPATCHED_AUTOMOD_FREQ:
			return "clipAutomodFreq";
		case UNPATCHED_AUTOMOD_MANUAL:
			return "clipAutomodManual";

		case UNPATCHED_ARP_GATE:
			return "arpGate";

		case UNPATCHED_NOTE_PROBABILITY:
			return "noteProbability";

		case UNPATCHED_ARP_BASS_PROBABILITY:
			return "bassProbability";

		case UNPATCHED_ARP_SWAP_PROBABILITY:
			return "swapProbability";

		case UNPATCHED_ARP_GLIDE_PROBABILITY:
			return "glideProbability";

		case UNPATCHED_REVERSE_PROBABILITY:
			return "reverseProbability";

		case UNPATCHED_ARP_CHORD_POLYPHONY:
			return "chordPolyphony";

		case UNPATCHED_ARP_CHORD_PROBABILITY:
			return "chordProbability";

		case UNPATCHED_ARP_RATCHET_PROBABILITY:
			return "ratchetProbability";

		case UNPATCHED_ARP_RATCHET_AMOUNT:
			return "ratchetAmount";

		case UNPATCHED_ARP_SEQUENCE_LENGTH:
			return "sequenceLength";

		case UNPATCHED_ARP_RHYTHM:
			return "rhythm";

		case UNPATCHED_ARP_SPREAD_GATE:
			return "spreadGate";

		case UNPATCHED_ARP_SPREAD_OCTAVE:
			return "spreadOctave";

		case UNPATCHED_SPREAD_VELOCITY:
			return "spreadVelocity";

		case UNPATCHED_SCATTER_ZONE_A:
			return "scatterZoneA";

		case UNPATCHED_SCATTER_ZONE_B:
			return "scatterZoneB";

		case UNPATCHED_SCATTER_MACRO_CONFIG:
			return "scatterDepth";

		case UNPATCHED_SCATTER_MACRO:
			return "scatterMacro";

		case UNPATCHED_SCATTER_PWRITE:
			return "scatterPWrite";

		case UNPATCHED_SCATTER_DENSITY:
			return "scatterDensity";

		case UNPATCHED_NUM_SHARED:
		    // Intentionally not handled
		    ;
		}
	}

	else if (param >= FIRST_GLOBAL && param <= GLOBAL_NONE) {
		// global patched params
		switch (static_cast<Global>(param)) {
		case GLOBAL_LFO_FREQ_1:
			return "lfo1Rate";

		case GLOBAL_LFO_FREQ_2:
			return "lfo3Rate";

		case GLOBAL_VOLUME_POST_FX:
			return "volumePostFX";

		case GLOBAL_VOLUME_POST_REVERB_SEND:
			return "volumePostReverbSend";

		case GLOBAL_DELAY_RATE:
			return "delayRate";

		case GLOBAL_DELAY_FEEDBACK:
			return "delayFeedback";

		case GLOBAL_SCATTER_MACRO:
			return "globalScatterMacro";

		case GLOBAL_AUTOMOD_DEPTH:
			return "globalAutomodMacro";
		case GLOBAL_AUTOMOD_FREQ:
			return "globalAutomodFreq";
		case GLOBAL_AUTOMOD_MANUAL:
			return "globalAutomodManual";

		case GLOBAL_SCATTER_ZONE_A:
			return "globalScatterZoneA";

		case GLOBAL_SCATTER_ZONE_B:
			return "globalScatterZoneB";

		case GLOBAL_SCATTER_MACRO_CONFIG:
			return "globalScatterDepth";

		case GLOBAL_SCATTER_PWRITE:
			return "globalScatterPWrite";

		case GLOBAL_SCATTER_DENSITY:
			return "globalScatterDensity";

		case GLOBAL_REVERB_AMOUNT:
			return "reverbAmount";

		case GLOBAL_MOD_FX_RATE:
			return "modFXRate";

		case GLOBAL_MOD_FX_DEPTH:
			return "modFXDepth";

		case GLOBAL_ARP_RATE:
			return "arpRate";

		case GLOBAL_NONE:
		    // Intentionally not handled
		    ;
		}
	}
	else if (param <= util::to_underlying(LOCAL_LAST)) {
		// Local patched params
		switch (static_cast<Local>(param)) {
		case LOCAL_OSC_A_VOLUME:
			return "oscAVolume";

		case LOCAL_OSC_B_VOLUME:
			return "oscBVolume";

		case LOCAL_VOLUME:
			return "volume";

		case LOCAL_NOISE_VOLUME:
			return "noiseVolume";

		case LOCAL_OSC_A_PHASE_WIDTH:
			return "oscAPhaseWidth";

		case LOCAL_OSC_B_PHASE_WIDTH:
			return "oscBPhaseWidth";

		case LOCAL_OSC_A_WAVE_INDEX:
			return "oscAWavetablePosition";

		case LOCAL_OSC_B_WAVE_INDEX:
			return "oscBWavetablePosition";

		case LOCAL_LPF_RESONANCE:
			return "lpfResonance";

		case LOCAL_HPF_RESONANCE:
			return "hpfResonance";

		case LOCAL_PAN:
			return "pan";

		case LOCAL_TABLE_SHAPER_DRIVE:
			return "tableShaperDrive";

		case LOCAL_SINE_SHAPER_DRIVE:
			return "sineShaperDrive";

		case LOCAL_TABLE_SHAPER_MIX:
			return "tableShaperMix";

		case LOCAL_SINE_SHAPER_TWIST:
			return "sineShaperTwist";

		case LOCAL_SINE_SHAPER_HARMONIC:
			return "patchedSineShaperHarmonic";

		case LOCAL_MODULATOR_0_VOLUME:
			return "modulator1Volume";

		case LOCAL_MODULATOR_1_VOLUME:
			return "modulator2Volume";

		case LOCAL_LPF_FREQ:
			return "lpfFrequency";

		case LOCAL_LPF_MORPH:
			return "lpfMorph";

		case LOCAL_HPF_MORPH:
			return "hpfMorph";

		case LOCAL_PITCH_ADJUST:
			return "pitch";

		case LOCAL_OSC_A_PITCH_ADJUST:
			return "oscAPitch";

		case LOCAL_OSC_B_PITCH_ADJUST:
			return "oscBPitch";

		case LOCAL_MODULATOR_0_PITCH_ADJUST:
			return "modulator1Pitch";

		case LOCAL_MODULATOR_1_PITCH_ADJUST:
			return "modulator2Pitch";

		case LOCAL_HPF_FREQ:
			return "hpfFrequency";

		case LOCAL_LFO_LOCAL_FREQ_1:
			return "lfo2Rate";

		case LOCAL_LFO_LOCAL_FREQ_2:
			return "lfo4Rate";

		case LOCAL_ENV_0_ATTACK:
			return "env1Attack";
		case LOCAL_ENV_1_ATTACK:
			return "env2Attack";
		case LOCAL_ENV_2_ATTACK:
			return "env3Attack";
		case LOCAL_ENV_3_ATTACK:
			return "env4Attack";

		case LOCAL_ENV_0_DECAY:
			return "env1Decay";
		case LOCAL_ENV_1_DECAY:
			return "env2Decay";
		case LOCAL_ENV_2_DECAY:
			return "env3Decay";
		case LOCAL_ENV_3_DECAY:
			return "env4Decay";

		case LOCAL_ENV_0_SUSTAIN:
			return "env1Sustain";
		case LOCAL_ENV_1_SUSTAIN:
			return "env2Sustain";
		case LOCAL_ENV_2_SUSTAIN:
			return "env3Sustain";
		case LOCAL_ENV_3_SUSTAIN:
			return "env4Sustain";

		case LOCAL_ENV_0_RELEASE:
			return "env1Release";
		case LOCAL_ENV_1_RELEASE:
			return "env2Release";
		case LOCAL_ENV_2_RELEASE:
			return "env3Release";
		case LOCAL_ENV_3_RELEASE:
			return "env4Release";

		case LOCAL_MODULATOR_0_FEEDBACK:
			return "modulator1Feedback";

		case LOCAL_MODULATOR_1_FEEDBACK:
			return "modulator2Feedback";

		case LOCAL_CARRIER_0_FEEDBACK:
			return "carrier1Feedback";

		case LOCAL_CARRIER_1_FEEDBACK:
			return "carrier2Feedback";

		case LOCAL_FOLD:
			return "waveFold";

		case LOCAL_LAST:
		    // Intentionally not handled
		    ;
		}
	}

	return "none";
}
char const* paramNameForFile(Kind const kind, ParamType const param) {
	return paramNameForFileConst(kind, param);
}
constexpr ParamType fileStringToParamConst(Kind kind, char const* name, bool allowPatched) {
	int32_t start = allowPatched ? 0 : UNPATCHED_START;
	for (int32_t p = start; p < kUnpatchedAndPatchedMaximum; ++p) {
		if (std::string_view(name) == paramNameForFileConst(kind, p)) {
			return p;
		}
	}

	if (std::string_view(name) == "range") {
		return util::to_underlying(PLACEHOLDER_RANGE); // For compatibility reading files from before V3.2.0
	}

	return util::to_underlying(GLOBAL_NONE);
}
ParamType fileStringToParam(Kind kind, char const* name, bool allowPatched) {
	return fileStringToParamConst(kind, name, allowPatched);
}
uint32_t expressionParamFromShortcut(int x, int y) {
	if (x == 14 && y == 7) {
		return X_PITCH_BEND;
	}
	else if (x == 15 && y == 0) {
		return Z_PRESSURE;
	}
	else if (x == 15 && y == 7) {
		return Y_SLIDE_TIMBRE;
	}
	return kNoParamID;
}

constexpr bool validateParams() {
	bool m = true;
	Kind kind = Kind::UNPATCHED_SOUND;
	for (int32_t p = 0; p < kUnpatchedAndPatchedMaximum; ++p) {
		auto name = paramNameForFileConst(kind, p);
		auto paramFromName = fileStringToParamConst(kind, name, true);
		if (p != paramFromName && std::string_view(name) != "none") {
			m = false;
		}
	}
	kind = Kind::UNPATCHED_GLOBAL;
	for (int32_t p = UNPATCHED_START; p < kUnpatchedAndPatchedMaximum; ++p) {
		auto name = paramNameForFileConst(kind, p);
		auto paramFromName = fileStringToParamConst(kind, name, false);
		if (p != paramFromName && std::string_view(name) != "none") {
			m = false;
		}
	}
	return m;
}
static_assert(validateParams());
} // namespace deluge::modulation::params
