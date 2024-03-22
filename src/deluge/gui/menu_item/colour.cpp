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

Colour activeColourMenu{l10n::String::STRING_FOR_ACTIVE};
Colour stoppedColourMenu{l10n::String::STRING_FOR_STOPPED};
Colour mutedColourMenu{l10n::String::STRING_FOR_MUTED};
Colour soloColourMenu{l10n::String::STRING_FOR_SOLOED};
Colour fillColourMenu{l10n::String::STRING_FOR_FILL};
Colour onceColourMenu{l10n::String::STRING_FOR_ONCE};

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
