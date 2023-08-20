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
#include "gui/colour.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"

namespace deluge::gui::menu_item {

Colour activeColourMenu{l10n::String::STRING_FOR_ACTIVE};
Colour stoppedColourMenu{l10n::String::STRING_FOR_STOPPED};
Colour mutedColourMenu{l10n::String::STRING_FOR_MUTED};
Colour soloColourMenu{l10n::String::STRING_FOR_SOLOED};

void Colour::getRGB(uint8_t rgb[3]) {
	switch (value) {
	case 0: // Red
		rgb[0] = disabledColour.r;
		rgb[1] = disabledColour.g;
		rgb[2] = disabledColour.b;
		break;

	case 1: // Green
		rgb[0] = enabledColour.r;
		rgb[1] = enabledColour.g;
		rgb[2] = enabledColour.b;
		break;

	case 2: // Blue
		rgb[0] = 0;
		rgb[1] = 0;
		rgb[2] = 255;
		break;

	case 3: // Yellow
		rgb[0] = mutedColour.r;
		rgb[1] = mutedColour.g;
		rgb[2] = mutedColour.b;
		break;

	case 4: // Cyan
		rgb[0] = 0;
		rgb[1] = 128;
		rgb[2] = 128;
		break;

	case 5: // Purple
		rgb[0] = 128;
		rgb[1] = 0;
		rgb[2] = 128;
		break;

	case 6: // Amber
		rgb[0] = 255;
		rgb[1] = 48;
		rgb[2] = 0;
		break;

	case 7: // White
		rgb[0] = 128;
		rgb[1] = 128;
		rgb[2] = 128;
		break;

	case 8: // Pink
		rgb[0] = 255;
		rgb[1] = 44;
		rgb[2] = 50;
		break;
	}
}
} // namespace deluge::gui::menu_item
