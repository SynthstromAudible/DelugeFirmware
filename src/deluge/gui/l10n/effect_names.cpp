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

#include "gui/l10n/l10n.h"

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

std::string_view mod_fx_param_name(effects::ModFXType type, effects::ModFxParam param, bool short_name) {
	using enum String;
	using effects::ModFxParam;
	if (type == effects::ModFXType::GRAIN) {
		switch (param) {
		case ModFxParam::DEPTH:
			return getView(short_name ? STRING_FOR_GRAIN_AMOUNT_SHORT : STRING_FOR_GRAIN_AMOUNT);
		case ModFxParam::FEEDBACK:
			return getView(short_name ? STRING_FOR_GRAIN_RANDOMNESS_SHORT : STRING_FOR_GRAIN_RANDOMNESS);
		case ModFxParam::OFFSET:
			return getView(short_name ? STRING_FOR_GRAIN_DENSITY_SHORT : STRING_FOR_GRAIN_DENSITY);
		}
	}
	else {
		switch (param) {
		case ModFxParam::DEPTH:
			return getView(short_name ? STRING_FOR_DEPTH_SHORT : STRING_FOR_DEPTH);
		case ModFxParam::FEEDBACK:
			return getView(short_name ? STRING_FOR_FEEDBACK_SHORT : STRING_FOR_FEEDBACK);
		case ModFxParam::OFFSET:
			return getView(short_name ? STRING_FOR_OFFSET_SHORT : STRING_FOR_OFFSET);
		}
	}
	return getView(STRING_FOR_NONE);
}

} // namespace deluge::l10n
