/*
 * Copyright (c) 2025 Leonid Burygin
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
#include "hid/display/oled_canvas/canvas.h"

#include <util/comparison.h>

using namespace deluge::hid::display;

namespace deluge::gui::menu_item::eq {
class EqMenu final : public HorizontalMenu {
public:
	EqMenu(l10n::String newName, std::initializer_list<MenuItem*> newItems) : HorizontalMenu(newName, newItems) {}

	void renderMenuItems(std::span<MenuItem*> items, const MenuItem* currentItem) override {
		const auto [bass, treble, bass_freq, treble_freq, order_changed] = ensureCorrectItemsOrderAndGetValues();
		if (order_changed) {
			renderOLED();
			return;
		}

		constexpr uint8_t start_y = OLED_MAIN_TOPMOST_PIXEL + kTextTitleSizeY + 5;
		constexpr uint8_t end_y = OLED_MAIN_HEIGHT_PIXELS - 6;
		constexpr uint8_t center_y = start_y + (end_y - start_y) / 2;
		constexpr uint8_t height = end_y - start_y;

		constexpr uint8_t padding_x = 4;
		constexpr uint8_t start_x = padding_x - 1;
		constexpr uint8_t end_x = OLED_MAIN_WIDTH_PIXELS - padding_x;
		constexpr uint8_t slope_width = 12;
		constexpr uint8_t bass_band_travel_width = (end_x - start_x) / 2 - slope_width;
		constexpr uint8_t treble_band_travel_width = (end_x - start_x) * 0.75f;

		constexpr uint8_t bass_x0 = start_x;
		uint8_t bass_x1 = std::lerp(bass_x0, bass_x0 + bass_band_travel_width, bass_freq);
		uint8_t bass_x2 = bass_x1 + slope_width;
		uint8_t bass_y1 = std::lerp(end_y, end_y - height, bass);
		uint8_t bass_y2 = center_y;

		constexpr uint8_t treble_x0 = end_x;
		uint8_t treble_x1 = std::lerp(end_x - treble_band_travel_width, end_x, treble_freq);
		uint8_t treble_x2 = treble_x1 - slope_width;
		uint8_t treble_y1 = std::lerp(end_y, end_y - height, treble);
		uint8_t treble_y2 = center_y;

		// Treble EQ also can affect mid & bass frequencies, and it has higher priority,
		// so we allow to move the treble band to the bass' territory
		if (bass_x2 > treble_x2) {
			const uint8_t diff = bass_x2 - treble_x2;
			bass_x2 -= diff;
			bass_x1 -= diff;
		}

		// If bass freq and treble freq adjustment points are close to each other, smoothly adjust their y positions
		// to morph between a slope line and a straight line
		auto center_between = [](uint8_t a, uint8_t b) { return std::min(a, b) + std::abs(b - a) / 2; };
		const float morph = 1.0f - (treble_x2 - bass_x2) / 14.f;
		if (morph > 0.f) {
			const uint8_t target_y = center_between(bass_y1, treble_y1);
			bass_y2 = std::lerp(bass_y2, target_y, morph);
			treble_y2 = std::lerp(treble_y2, target_y, morph);
		}

		oled_canvas::Canvas& image = OLED::main;
		image.drawLine(bass_x0, bass_y1, bass_x1, bass_y1);
		image.drawLine(bass_x1, bass_y1, bass_x2, bass_y2);
		image.drawLine(bass_x2, bass_y2, treble_x2, treble_y2);
		image.drawLine(treble_x2, treble_y2, treble_x1, treble_y1);
		image.drawLine(treble_x1, treble_y1, treble_x0, treble_y1);

		// Draw reference lines
		if (std::abs(center_y - bass_y1) > 1) {
			for (uint8_t x = 0; x <= bass_x2; x++) {
				if (x % 6 == 3 && std::abs(x - bass_x2) > 1 && std::abs(x - treble_x2) > 1) {
					image.drawPixel(x, center_y);
				}
			}
		}
		if (std::abs(center_y - treble_y1) > 1) {
			for (uint8_t x = 0; x <= end_x; x++) {
				if (x % 6 == 3 && std::abs(x - bass_x2) > 1 && std::abs(x - treble_x2) > 1) {
					image.drawPixel(x, center_y);
				}
			}
		}
		for (uint8_t y = start_y - 1; y <= end_y + 1; y += 4) {
			image.drawPixel(bass_x2, y);
			image.drawPixel(treble_x2, y);
		}

		// Draw control indicators
		selected_x_ = -1, selected_y_ = -1;
		drawControlIndicator(center_between(bass_x0, bass_x1), bass_y1, currentItem == items[0]);
		drawControlIndicator(bass_x2, bass_y2, currentItem == items[1]);
		drawControlIndicator(treble_x2, treble_y2, currentItem == items[2]);
		drawControlIndicator(center_between(treble_x1, treble_x0), treble_y1, currentItem == items[3]);
	}

private:
	int32_t selected_x_, selected_y_;

	struct EqualizerValues {
		float bass{0.f};
		float treble{0.f};
		float bass_freq{0.f};
		float treble_freq{0.f};
		bool order_changed{false};
	};

	EqualizerValues ensureCorrectItemsOrderAndGetValues() {
		using namespace deluge::modulation;

		const uint8_t current_item_pos = std::distance(items.begin(), current_item_);
		UnpatchedParam* desired_order_items[4] = {nullptr, nullptr, nullptr, nullptr};
		EqualizerValues result{};

		for (auto* i : items) {
			switch (const auto as_unpatched = static_cast<UnpatchedParam*>(i); as_unpatched->getP()) {
			case params::UNPATCHED_BASS:
				desired_order_items[0] = as_unpatched;
				result.bass = as_unpatched->getValue() / 50.f;
				break;
			case params::UNPATCHED_BASS_FREQ:
				desired_order_items[1] = as_unpatched;
				result.bass_freq = as_unpatched->getValue() / 50.f;
				break;
			case params::UNPATCHED_TREBLE_FREQ:
				desired_order_items[2] = as_unpatched;
				// Treble boost has no effect on treble freq's values above 32
				result.treble_freq = std::clamp<int32_t>(as_unpatched->getValue(), 0, 32) / 32.f;
				break;
			case params::UNPATCHED_TREBLE:
				desired_order_items[3] = as_unpatched;
				result.treble = as_unpatched->getValue() / 50.f;
				break;
			default:
				break;
			}
		}

		for (int idx = 0; idx < items.size(); ++idx) {
			if (items[idx] != desired_order_items[idx] && desired_order_items[idx]) {
				items[idx] = desired_order_items[idx];
				result.order_changed = true;
			}
		}

		if (result.order_changed) {
			current_item_ = items.begin() + current_item_pos;
			lastSelectedItemPosition = kNoSelection;
		}

		return result;
	}

	void drawControlIndicator(const float center_x, const float center_y, const bool is_selected) {
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

} // namespace deluge::gui::menu_item::eq