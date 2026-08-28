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

// FACADE (remove after UI phase 5): all the drawing lives in gfx/oled/canvas.cpp. What is left here is the one
// primitive that reads a firmware setting, so it cannot be a plain forwarder.

#include "canvas.h"

namespace deluge::hid::display::oled_canvas {

/// inverts just the left edge
void Canvas::invertLeftEdgeForMenuHighlighting(int32_t xMin, int32_t width, int32_t startY, int32_t endY) {
	if (!menuHighlightingInverts) {
		draw_vertical_line(xMin, startY, endY);
	}
	else {
		invertAreaRounded(xMin, width, startY, endY);
	}
}

} // namespace deluge::hid::display::oled_canvas
