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
#include "effects/global_effectable/params.h"
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "instruments/deluge/params.h"
#include "model/mod_controllable/filters/filter_config.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/song/song.h"
#include "util/container/enum_to_string_map.hpp"
#include <cstring>
#include <span>

namespace deluge::modulation::params {

namespace {
/// The row of @p table with id @p id, looked for first at @p index: the tables keep each block's rows dense and in id
/// order, so a param's value changes (which ask for its smoothing flags) find it without a scan.
const plugin::ParamInfo* rowNear(std::span<const plugin::ParamInfo> table, std::size_t index, uint32_t id) {
	if (index < table.size() && table[index].id.value == id) {
		return &table[index];
	}
	return plugin::find_param(table, plugin::ParamId{id});
}
} // namespace

const plugin::ParamInfo* paramInfoFor(Kind kind, int32_t paramId) {
	if (paramId < 0) {
		return nullptr;
	}
	const auto index = static_cast<std::size_t>(paramId);
	const auto unpatchedId = static_cast<uint32_t>(UNPATCHED_START + paramId);
	switch (kind) {
	case Kind::PATCHED:
		return rowNear(instrument::kParams, index, static_cast<uint32_t>(paramId));
	case Kind::NONE:
	case Kind::UNPATCHED_SOUND:
		return rowNear(instrument::kParams, instrument::kParamBlocks[0].count + index, unpatchedId);
	case Kind::UNPATCHED_GLOBAL:
		return rowNear(global_effectable::kParams, index, unpatchedId);
	default:
		return nullptr;
	}
}

bool hasParamFlag(Kind kind, int32_t paramId, plugin::ParamFlag flag) {
	const plugin::ParamInfo* info = paramInfoFor(kind, paramId);
	return info != nullptr && info->flags.has(flag);
}

bool isParamBipolar(Kind kind, int32_t paramID) {
	return (kind == Kind::PATCH_CABLE) || isParamPitchBend(kind, paramID)
	       || hasParamFlag(kind, paramID, plugin::ParamFlag::CENTRED);
}

bool isParamArpRhythm(Kind kind, int32_t paramID) {
	return (kind == Kind::UNPATCHED_SOUND && paramID == UNPATCHED_ARP_RHYTHM);
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
	       && (modControllableAudio->effects().stutter.use_song_stutter
	               ? currentSong->live.global_effectable.effects().stutter.quantized
	               : modControllableAudio->effects().stutter.quantized);
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

char const* getPatchedParamShortName(ParamType type, ModControllableAudio* modControllable) {
	if (modControllable != nullptr) {
		// See getParamDisplayName: the ladder filters rename morph after what it actually does.
		if (type == LOCAL_LPF_MORPH
		    && model::filter::SpecificFilter(modControllable->modControllableSnapshot().effects.filter.lpf_mode)
		               .getFamily()
		           == model::filter::FilterFamily::LP_LADDER) {
			return "LPF Drive";
		}
		if (type == LOCAL_HPF_MORPH
		    && model::filter::SpecificFilter(modControllable->modControllableSnapshot().effects.filter.hpf_mode)
		               .getFamily()
		           == model::filter::FilterFamily::HP_LADDER) {
			return "HPF FM";
		}
	}

	return getPatchedParamShortName(type);
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

char const* getParamDisplayName(Kind kind, int32_t p, ModControllableAudio* modControllable) {
	using enum l10n::String;

	if (modControllable != nullptr) {
		bool isLPFMorph = (kind == Kind::PATCHED && p == LOCAL_LPF_MORPH)
		                  || (kind == Kind::UNPATCHED_GLOBAL && p == UnpatchedGlobal::UNPATCHED_LPF_MORPH);
		bool isHPFMorph = (kind == Kind::PATCHED && p == LOCAL_HPF_MORPH)
		                  || (kind == Kind::UNPATCHED_GLOBAL && p == UnpatchedGlobal::UNPATCHED_HPF_MORPH);

		// The ladder filters repurpose morph, so they name it after what it actually does. The SVF modes really do
		// morph, and a filter which is off has no morph param at all, so both keep the default name.
		if (isLPFMorph) {
			if (model::filter::SpecificFilter(modControllable->modControllableSnapshot().effects.filter.lpf_mode)
			        .getFamily()
			    == model::filter::FilterFamily::LP_LADDER) {
				return l10n::get(STRING_FOR_LPF_DRIVE);
			}
		}
		else if (isHPFMorph) {
			if (model::filter::SpecificFilter(modControllable->modControllableSnapshot().effects.filter.hpf_mode)
			        .getFamily()
			    == model::filter::FilterFamily::HP_LADDER) {
				return l10n::get(STRING_FOR_HPF_FM);
			}
		}
	}

	return getParamDisplayName(kind, p);
}

bool paramNeedsLPF(ParamType p, bool fromAutomation) {
	// Volumes and the like smooth a user's change so it doesn't step, but take a step of automation at once so the
	// step lands at its value
	return hasParamFlag(Kind::PATCHED, p, plugin::ParamFlag::SMOOTHED)
	       || (!fromAutomation && hasParamFlag(Kind::PATCHED, p, plugin::ParamFlag::SMOOTHED_USER_ONLY));
}

/// @brief The name the instrument's or the global effectable's table gives id @p param, or "none".
constexpr char const* tableName(std::span<const plugin::ParamInfo> table, ParamType param) {
	const plugin::ParamInfo* row = plugin::find_param(table, plugin::ParamId{param});
	return row != nullptr ? row->file_name.data() : "none";
}

constexpr char const* paramNameForFileConst(Kind const kind, ParamType const param, bool forMidiFollowFile = false) {
	using enum Kind;
	// Patched params have one name whichever collection asks
	if (param < UNPATCHED_START) {
		return tableName(instrument::kParams, param);
	}
	if (kind == UNPATCHED_GLOBAL) {
		const auto unpatched = static_cast<UnpatchedGlobal>(param - UNPATCHED_START);
		if (forMidiFollowFile && unpatched == UNPATCHED_VOLUME) {
			return "volumePostFX"; // MIDIFollow.XML's vocabulary
		}
		if (forMidiFollowFile && unpatched == UNPATCHED_PITCH_ADJUST) {
			return "pitch"; // MIDIFollow.XML's vocabulary
		}
		if (unpatched == UNPATCHED_TEMPO) {
			// A learned knob or a MIDI follow mapping can't name the tempo; only the param block writes it, from the
			// codec's own table
			return "none";
		}
		return tableName(global_effectable::kParams, param);
	}
	// A sound's own unpatched params only for a sound's collection; the shared ones for any
	if (kind == UNPATCHED_SOUND || param < UNPATCHED_START + UNPATCHED_NUM_SHARED) {
		return tableName(instrument::kParams, param);
	}
	return "none";
}
char const* paramNameForFile(Kind const kind, ParamType const param, bool forMidiFollowFile) {
	return paramNameForFileConst(kind, param, forMidiFollowFile);
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
