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
		constexpr int32_t padding_x = 4;
		constexpr int32_t start_x = padding_x;
		constexpr uint8_t start_y = OLED_MAIN_TOPMOST_PIXEL + kTextTitleSizeY + 7;
		constexpr uint8_t end_y = OLED_MAIN_HEIGHT_PIXELS - 6;
		constexpr int32_t draw_width = OLED_MAIN_WIDTH_PIXELS - 2 * padding_x;
		constexpr uint8_t draw_height = end_y - start_y;
		constexpr float max_segment_width = draw_width / 4;

		// Calculate widths
		const float attack_width = attack / 50.0f * max_segment_width;
		const float decay_normalized = sigmoidLikeCurve(decay, 50.0f, 8.0f); // Maps 0-50 to 0-1 range with steep start
		const float decay_width = decay_normalized * max_segment_width;

		// X positions
		const float attack_x = round(start_x + attack_width);
		const float decay_x = round(attack_x + decay_width);
		constexpr float sustain_x = start_x + max_segment_width * 3; // Fixed sustainX position
		// Make release x dynamic, right of sustain
		const float release_x = round(sustain_x + release / 50.0f * (start_x + draw_width - sustain_x));

		// Y positions
		constexpr int32_t base_y = start_y + draw_height;
		const int32_t sustain_y = base_y - round(sustain / 50.0f * draw_height);

		oled_canvas::Canvas& image = OLED::main;

		// Draw stage lines
		image.drawLine(start_x, base_y, attack_x, start_y);
		image.drawLine(attack_x, start_y, decay_x, sustain_y);
		image.drawLine(decay_x, sustain_y, sustain_x, sustain_y);
		image.drawLine(sustain_x, sustain_y, release_x, base_y);
		image.drawLine(release_x, base_y, start_x + draw_width, base_y);

		// Draw stage transition point dotted lines
		for (int32_t y = OLED_MAIN_VISIBLE_HEIGHT + 1; y >= start_y - 2; y -= 4) {
			// reduce a messy look when lines are close to each other by omitting the line
			if (attack_x > start_x + 3) {
				image.drawPixel(attack_x, y);
			}
			if (decay_x - attack_x > 4) {
				image.drawPixel(decay_x, y);
			}
			if (sustain_y > start_y || y > sustain_y) {
				image.drawPixel(sustain_x, y);
			}
		}

		// Draw transition indicators
		selected_x_ = -1, selected_y_ = -1;
		const int32_t selected_pos = std::distance(items.begin(), std::ranges::find(items, currentItem));

		drawTransitionIndicator(attack_x, start_y, selected_pos == 0);
		drawTransitionIndicator(decay_x, sustain_y, selected_pos == 1);
		drawTransitionIndicator(decay_x + (sustain_x - decay_x) / 2, sustain_y, selected_pos == 2);
		drawTransitionIndicator(release_x, base_y, selected_pos == 3);
	}

private:
	int32_t selected_x_, selected_y_;

	void drawTransitionIndicator(const float center_x, const float center_y, const bool is_selected) {
		oled_canvas::Canvas& image = OLED::main;

		const int32_t ix = static_cast<int32_t>(center_x);
		const int32_t iy = static_cast<int32_t>(center_y);

		if (!is_selected && ix == selected_x_ && iy == selected_y_) {
			// Overlap occurred, skip drawing
			return;
		}

		// Clear region inside
		constexpr int32_t square_size = 2;
		constexpr int32_t innerSquareSize = square_size - 1;
		for (int32_t x = ix - innerSquareSize; x <= ix + innerSquareSize; x++) {
			for (int32_t y = iy - innerSquareSize; y <= iy + innerSquareSize; y++) {
				image.clearPixel(x, y);
			}
		}

		if (is_selected) {
			// Invert region inside to highlight selection
			selected_x_ = ix, selected_y_ = iy;
			image.invertArea(ix - innerSquareSize, square_size * 2 - 1, iy - innerSquareSize, iy + innerSquareSize);
		}

		// Draw a transition square
		image.drawRectangle(ix - square_size, iy - square_size, ix + square_size, iy + square_size);
	}
};

} // namespace deluge::gui::menu_item::envelope
