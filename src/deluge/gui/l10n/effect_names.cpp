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

#include "gui/l10n/effect_names.h"

#include "effects/reverb_presets.h"
#include "gui/l10n/l10n.h"

#include <array>
#include <utility>

namespace deluge::l10n {

std::string_view mod_fx_name(effects::ModFXType type) {
	using enum String;
	switch (type) {
	case effects::ModFXType::FLANGER:
		return getView(STRING_FOR_FLANGER);
	case effects::ModFXType::PHASER:
		return getView(STRING_FOR_PHASER);
	case effects::ModFXType::CHORUS:
		return getView(STRING_FOR_CHORUS);
	case effects::ModFXType::CHORUS_STEREO:
		return getView(STRING_FOR_STEREO_CHORUS);
	case effects::ModFXType::DIMENSION:
		return getView(STRING_FOR_DIMENSION);
	case effects::ModFXType::GRAIN:
		return getView(STRING_FOR_GRAIN);
	case effects::ModFXType::WARBLE:
		return getView(STRING_FOR_WARBLE);
	case effects::ModFXType::NONE:
		break;
	}
	return getView(STRING_FOR_DISABLED);
}

namespace {

/// A param's long and short display strings.
struct ParamStrings {
	String long_name;  ///< The full name.
	String short_name; ///< The short form for popups and column labels.
};

using ParamTable = std::array<ParamStrings, effects::kNumModFxParams>;

/// The delay-line types' names, indexed by ModFxParam.
constexpr ParamTable kLineNames{{
    {String::STRING_FOR_DEPTH, String::STRING_FOR_DEPTH_SHORT},
    {String::STRING_FOR_FEEDBACK, String::STRING_FOR_FEEDBACK_SHORT},
    {String::STRING_FOR_OFFSET, String::STRING_FOR_OFFSET_SHORT},
}};

/// The grain's names for the same knobs, indexed by ModFxParam.
constexpr ParamTable kGrainNames{{
    {String::STRING_FOR_GRAIN_AMOUNT, String::STRING_FOR_GRAIN_AMOUNT_SHORT},
    {String::STRING_FOR_GRAIN_RANDOMNESS, String::STRING_FOR_GRAIN_RANDOMNESS_SHORT},
    {String::STRING_FOR_GRAIN_DENSITY, String::STRING_FOR_GRAIN_DENSITY_SHORT},
}};

} // namespace

std::string_view mod_fx_param_name(effects::ModFXType type, effects::ModFxParam param, bool short_name) {
	const auto index = static_cast<size_t>(std::to_underlying(param));
	if (index >= effects::kNumModFxParams) {
		return getView(String::STRING_FOR_NONE);
	}
	const ParamStrings& names = (type == effects::ModFXType::GRAIN ? kGrainNames : kLineNames)[index];
	return getView(short_name ? names.short_name : names.long_name);
}

std::string_view filter_morph_name(effects::FilterMode mode, bool short_name) {
	using enum String;
	switch (effects::filter_family(mode)) {
	case effects::FilterFamily::LP_LADDER:
		return getView(STRING_FOR_DRIVE);
	case effects::FilterFamily::HP_LADDER:
		return getView(STRING_FOR_FM);
	case effects::FilterFamily::SVF:
		return getView(short_name ? STRING_FOR_MORPH_SHORT : STRING_FOR_MORPH);
	case effects::FilterFamily::NONE:
		break;
	}
	return getView(STRING_FOR_NONE);
}

std::string_view reverb_preset_name(size_t preset) {
	static constexpr std::array<String, effects::kReverbPresets.size()> kNames{String::STRING_FOR_SMALL_ROOM_REVERB,
	                                                                           String::STRING_FOR_MEDIUM_ROOM_REVERB,
	                                                                           String::STRING_FOR_LARGE_ROOM_REVERB};
	return getView(kNames[preset % kNames.size()]);
}

} // namespace deluge::l10n
