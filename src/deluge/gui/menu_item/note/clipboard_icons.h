/*
 * Copyright © 2026 Sean Ditny
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

#pragma once
#include <cstdint>

namespace deluge::gui::menu_item::note {
enum class ClipboardOperation { CUT, COPY, PASTE_FIRMLY, PASTE_GENTLY, DELETE };

// All icons fit an 18 x 15 pixel horizontal-menu content area.
template <typename Canvas>
void drawClipboardIcon(Canvas& canvas, int32_t x, int32_t y, ClipboardOperation operation, uint8_t noteCount = 0) {
	switch (operation) {
	case ClipboardOperation::COPY:
		canvas.drawLine(x + 1, y + 3, x + 1, y + 14);
		canvas.drawLine(x + 1, y + 14, x + 12, y + 14);
		canvas.drawLine(x + 12, y + 12, x + 12, y + 14);
		canvas.drawLine(x + 1, y + 3, x + 3, y + 3);
		canvas.drawRectangle(x + 4, y, x + 16, y + 11);
		for (int32_t line = 3; line <= 9; line += 3) {
			canvas.drawLine(x + 6, y + line, x + 14, y + line);
		}
		break;
	case ClipboardOperation::CUT:
		canvas.drawCircle(x + 4, y + 11, 3);
		canvas.drawCircle(x + 13, y + 11, 3);
		canvas.drawLine(x + 5, y + 8, x + 14, y);
		canvas.drawLine(x + 12, y + 8, x + 3, y);
		canvas.drawPixel(x + 8, y + 5);
		break;
	case ClipboardOperation::PASTE_FIRMLY:
	case ClipboardOperation::PASTE_GENTLY:
		canvas.drawRectangle(x, y + 2, x + 17, y + 14);
		canvas.drawRectangle(x + 6, y, x + 11, y + 2);
		if (noteCount == 1) {
			// A 6 x 7 note centered in the 16 x 11 clipboard interior.
			canvas.drawLine(x + 11, y + 5, x + 11, y + 10);
			canvas.drawLine(x + 7, y + 9, x + 11, y + 9);
			canvas.drawLine(x + 6, y + 10, x + 11, y + 10);
			canvas.drawLine(x + 6, y + 11, x + 10, y + 11);
		}
		else if (noteCount > 1) {
			// Center the complete group and each row, with equal outer padding.
			// Four notes use 2 + 2; five use 3 + 2; six or more use 3 + 3.
			int32_t count = noteCount > 6 ? 6 : noteCount;
			int32_t rows = count > 3 ? 2 : 1;
			int32_t firstRow = count == 4 ? 2 : (count > 3 ? 3 : count);
			int32_t height = rows == 1 ? 5 : 4;
			for (int32_t row = 0; row < rows; ++row) {
				int32_t inRow = row == 0 ? firstRow : count - firstRow;
				int32_t gap = inRow == 3 ? 1 : 2;
				int32_t width = inRow * 4 + (inRow - 1) * gap;
				int32_t left = x + (18 - width) / 2;
				int32_t top = y + (rows == 1 ? 6 : 4 + row * 5);
				for (int32_t note = 0; note < inRow; ++note) {
					int32_t nx = left + note * (4 + gap);
					canvas.drawLine(nx + 3, top, nx + 3, top + height - 2);
					canvas.drawLine(nx + 1, top + height - 2, nx + 3, top + height - 2);
					canvas.drawLine(nx, top + height - 1, nx + 2, top + height - 1);
				}
			}
		}
		break;
	case ClipboardOperation::DELETE:
		canvas.drawLine(x + 2, y + 2, x + 16, y + 2);
		canvas.drawRectangle(x + 6, y, x + 12, y + 1);
		canvas.drawRectangle(x + 4, y + 4, x + 14, y + 14);
		canvas.drawLine(x + 7, y + 6, x + 7, y + 12);
		canvas.drawLine(x + 11, y + 6, x + 11, y + 12);
		break;
	}
}
} // namespace deluge::gui::menu_item::note
