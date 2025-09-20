/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#include "gui/menu_item/horizontal_menu.h"
#include "hid/display/oled.h"
#include "segment.h"

using namespace deluge::hid::display;

namespace deluge::gui::menu_item::envelope {
class EnvelopeMenu final : public HorizontalMenu, FormattedTitle {
public:
	EnvelopeMenu(l10n::String newName, std::span<MenuItem*> newItems, int32_t envelopeIndex)
	    : HorizontalMenu(newName, newItems), FormattedTitle(newName, envelopeIndex + 1) {}

	[[nodiscard]] std::string_view getName() const override { return FormattedTitle::title(); }
	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	void renderMenuItems(std::span<MenuItem*> items, const MenuItem* currentItem) override {
		// Get the values in 0-50 range
		const int32_t attack = static_cast<Segment*>(items[0])->getValue();
		const int32_t decay = static_cast<Segment*>(items[1])->getValue();
		const int32_t sustain = static_cast<Segment*>(items[2])->getValue();
		const int32_t release = static_cast<Segment*>(items[3])->getValue();

		// Constants
		constexpr int32_t totalWidth = OLED_MAIN_WIDTH_PIXELS;
		constexpr int32_t totalHeight = OLED_MAIN_VISIBLE_HEIGHT - kTextTitleSizeY - 6;
		constexpr int32_t padding = 4;
		constexpr int32_t drawX = padding;
		constexpr int32_t drawY = OLED_MAIN_TOPMOST_PIXEL + kTextTitleSizeY + padding + 3;
		constexpr int32_t drawWidth = totalWidth - 2 * padding;
		constexpr int32_t drawHeight = totalHeight - 2 * padding;
		constexpr float maxSegmentWidth = drawWidth / 4;

		// Calculate widths
		const float attackWidth = (attack / 50.0f) * maxSegmentWidth;
		const float decayNormalized = sigmoidLikeCurve(decay, 50.0f, 10.0f); // Maps 0-50 to 0-1 range with steep start
		const float decayWidth = decayNormalized * maxSegmentWidth;

		// X positions
		const float attackX = round(drawX + attackWidth);
		const float decayX = round(attackX + decayWidth);
		constexpr float sustainX = drawX + maxSegmentWidth * 3; // Fixed sustainX position
		const float releaseX = round(
		    sustainX + (release / 50.0f) * (drawX + drawWidth - sustainX)); // Make releaseX dynamic, right of sustain

		// Y positions
		constexpr int32_t baseY = drawY + drawHeight;
		constexpr int32_t peakY = drawY;
		const int32_t sustainY = baseY - round((sustain / 50.0f) * drawHeight);

		oled_canvas::Canvas& image = OLED::main;

		// Draw stage lines
		image.drawLine(drawX, baseY, attackX, peakY);
		image.drawLine(attackX, peakY, decayX, sustainY);
		image.drawLine(decayX, sustainY, sustainX, sustainY);
		image.drawLine(sustainX, sustainY, releaseX, baseY);
		image.drawLine(releaseX, baseY, drawX + drawWidth, baseY);

		// Draw stage transition point dotted lines
		for (int32_t y = OLED_MAIN_VISIBLE_HEIGHT; y >= drawY; y -= 4) {
			// reduce a messy look when lines are close to each other by omitting the line
			if (attackX > drawX + 3) {
				image.drawPixel(attackX, y);
			}
			if (decayX - attackX > 4) {
				image.drawPixel(decayX, y);
			}
			image.drawPixel(sustainX, y);
		}

		// Draw transition squares
		selectedX = -1, selectedY = -1;
		const int32_t selectedPos = std::distance(items.begin(), std::ranges::find(items, currentItem));

		// Attack → Decay
		drawTransitionSquare(attackX, peakY, selectedPos == 0);
		// Decay → Sustain
		drawTransitionSquare(decayX, sustainY, selectedPos == 1);
		// Sustain
		drawTransitionSquare(decayX + (sustainX - decayX) / 2, sustainY, selectedPos == 2);
		// Release → End
		drawTransitionSquare(releaseX, baseY, selectedPos == 3);
	}

private:
	int32_t selectedX, selectedY;

	void drawTransitionSquare(const float centerX, const float centerY, const bool isSelected) {
		oled_canvas::Canvas& image = OLED::main;

		const int32_t ix = static_cast<int32_t>(centerX);
		const int32_t iy = static_cast<int32_t>(centerY);

		if (!isSelected && ix == selectedX && iy == selectedY) {
			// Overlap occurred, skip drawing
			return;
		}

		// Clear region inside
		constexpr int32_t squareSize = 2, innerSquareSize = squareSize - 1;
		for (int32_t x = ix - innerSquareSize; x <= ix + innerSquareSize; x++) {
			for (int32_t y = iy - innerSquareSize; y <= iy + innerSquareSize; y++) {
				image.clearPixel(x, y);
			}
		}

		if (isSelected) {
			// Invert region inside to highlight selection
			selectedX = ix, selectedY = iy;
			image.invertArea(ix - innerSquareSize, (squareSize * 2) - 1, iy - innerSquareSize, iy + innerSquareSize);
		}

		// Draw a transition square
		image.drawRectangle(ix - squareSize, iy - squareSize, ix + squareSize, iy + squareSize);
	}
};

} // namespace deluge::gui::menu_item::envelope
