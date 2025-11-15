/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

#include "colour.h"
#include "gui/colour/colour.h"
#include "gui/colour/palette.h"

namespace deluge::gui::menu_item {

PLACE_SDRAM_BSS Colour activeColourMenu{l10n::String::STRING_FOR_ACTIVE};
PLACE_SDRAM_BSS Colour stoppedColourMenu{l10n::String::STRING_FOR_STOPPED};
PLACE_SDRAM_BSS Colour mutedColourMenu{l10n::String::STRING_FOR_MUTED};
PLACE_SDRAM_BSS Colour soloColourMenu{l10n::String::STRING_FOR_SOLOED};
PLACE_SDRAM_BSS Colour fillColourMenu{l10n::String::STRING_FOR_FILL};
PLACE_SDRAM_BSS Colour onceColourMenu{l10n::String::STRING_FOR_ONCE};

RGB Colour::getRGB() const {
	switch (value) {
	case RED: // Red
		return colours::red;

	case GREEN: // Green
		return colours::enabled;

	case BLUE: // Blue
		return colours::blue;

	case YELLOW: // Yellow
		return colours::yellow_orange;

	case CYAN: // Cyan
		return colours::cyan;

	case MAGENTA: // Purple
		return colours::magenta;

	case AMBER: // Amber
		return colours::amber;

	case WHITE: // White
		return colours::white;

	case PINK: // Pink
		return colours::pink;

	default:
		__builtin_unreachable(); // TODO: should be std::unreachable() with C++23
	}
}
} // namespace deluge::gui::menu_item
