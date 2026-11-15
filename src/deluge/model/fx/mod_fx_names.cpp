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

#include "model/fx/mod_fx_names.h"
#include "gui/l10n/l10n.h"

namespace modfx {
const char* getParamName(ModFXType type, ModFXParam param, bool shortName) {
	using enum deluge::l10n::String;
	using namespace deluge;

	switch (type) {
	case ModFXType::GRAIN: {
		switch (param) {
		case ModFXParam::DEPTH:
			return l10n::get(shortName ? STRING_FOR_GRAIN_AMOUNT_SHORT : STRING_FOR_GRAIN_AMOUNT);
		case ModFXParam::FEEDBACK:
			return l10n::get(shortName ? STRING_FOR_GRAIN_RANDOMNESS_SHORT : STRING_FOR_GRAIN_RANDOMNESS);
		case ModFXParam::OFFSET:
			return l10n::get(shortName ? STRING_FOR_GRAIN_DENSITY_SHORT : STRING_FOR_GRAIN_DENSITY);
		default:
			return l10n::get(STRING_FOR_NONE);
		}
	}

	default: {
		switch (param) {
		case ModFXParam::DEPTH:
			return l10n::get(shortName ? STRING_FOR_DEPTH_SHORT : STRING_FOR_DEPTH);
		case ModFXParam::FEEDBACK:
			return l10n::get(shortName ? STRING_FOR_FEEDBACK_SHORT : STRING_FOR_FEEDBACK);
		case ModFXParam::OFFSET:
			return l10n::get(shortName ? STRING_FOR_OFFSET_SHORT : STRING_FOR_OFFSET);
		default:
			return l10n::get(STRING_FOR_NONE);
		}
	}
	}
}

const char* modFXToString(ModFXType type) {
	switch (type) {
		using namespace deluge;
		using enum deluge::l10n::String;
	case ModFXType::FLANGER:
		return l10n::get(STRING_FOR_FLANGER);
	case ModFXType::PHASER:
		return l10n::get(STRING_FOR_PHASER);
	case ModFXType::CHORUS:
		return l10n::get(STRING_FOR_CHORUS);
	case ModFXType::CHORUS_STEREO:
		return l10n::get(STRING_FOR_STEREO_CHORUS);
	case ModFXType::DIMENSION:
		return l10n::get(STRING_FOR_DIMENSION);
	case ModFXType::GRAIN:
		return l10n::get(STRING_FOR_GRAIN);
	case ModFXType::WARBLE:
		return l10n::get(STRING_FOR_WARBLE);
	default:
		return l10n::get(STRING_FOR_DISABLED);
	}
}
} // namespace modfx
