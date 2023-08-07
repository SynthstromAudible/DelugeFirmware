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

#include "color.h"
#include "gui/color/color.h"
#include "gui/color/palette.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"

namespace deluge::gui::menu_item {

Color activeColorMenu{"ACTIVE"};
Color stoppedColorMenu{"STOPPED"};
Color mutedColorMenu{"MUTED"};
Color soloColorMenu{"SOLOED"};

RGB Color::getRGB() const {
	switch (value) {
	case RED: // Red
		return colors::red;

	case GREEN: // Green
		return colors::enabled;

	case BLUE: // Blue
		return colors::blue;

	case YELLOW: // Yellow
		return colors::yellow_orange;

	case CYAN: // Cyan
		return colors::cyan;

	case MAGENTA: // Purple
		return colors::magenta;

	case AMBER: // Amber
		return colors::amber;

	case WHITE: // White
		return colors::white;

	case PINK: // Pink
		return colors::pink;
	}
}
} // namespace deluge::gui::menu_item
