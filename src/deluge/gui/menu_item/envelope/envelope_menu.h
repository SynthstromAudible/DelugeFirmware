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
	/// Items: attack, decay, sustain, release segments, then any settings (mode, curve) on the next page
	EnvelopeMenu(l10n::String newName, std::span<MenuItem*> newItems, int32_t envelopeIndex)
	    : HorizontalMenu(newName, newItems, FIXED), FormattedTitle(newName, envelopeIndex + 1),
	      envelope_index_(envelopeIndex) {}

	[[nodiscard]] std::string_view getName() const override { return FormattedTitle::title(); }
	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	void renderMenuItems(std::span<MenuItem*> items, const MenuItem* currentItem) override {
		// Only the page of segments gets the graph
		if (items.size() < 4 || items[0] != getItems()[0]) {
			HorizontalMenu::renderMenuItems(items, currentItem);
			return;
		}

		using modulation::EnvelopeMode;
		const auto& config = soundEditor.currentSound->envelopeConfig[envelope_index_];

		// Get the values in 0-50 range
		const int32_t attack = static_cast<Segment&>(*items[0]).getValue();
		const int32_t decay = static_cast<Segment&>(*items[1]).getValue(); // hold in AHD
		const int32_t sustain = static_cast<Segment&>(*items[2]).getValue();
		const int32_t release = static_cast<Segment&>(*items[3]).getValue(); // decay in AHD

		// Constants
		constexpr int32_t padding_x = 4;
		constexpr int32_t start_x = padding_x;
		constexpr uint8_t start_y = OLED_MAIN_TOPMOST_PIXEL + kTextTitleSizeY + 7;
		constexpr uint8_t end_y = OLED_MAIN_HEIGHT_PIXELS - 6;
		constexpr int32_t draw_width = OLED_MAIN_WIDTH_PIXELS - 2 * padding_x;
		constexpr uint8_t draw_height = end_y - start_y;
		constexpr float max_segment_width = draw_width / 4;
		constexpr int32_t base_y = start_y + draw_height;
		constexpr float end_x = start_x + draw_width;

		auto level_y = [&](float level) { return static_cast<int32_t>(round(base_y - level * draw_height)); };

		// Widths
		const float attack_width = attack / 50.0f * max_segment_width;
		const float decay_normalized = sigmoidLikeCurve(decay, 50.0f, 8.0f); // Maps 0-50 to 0-1 range with steep start
		const float decay_width = decay_normalized * max_segment_width;

		const float attack_x = round(start_x + attack_width);
		const float sustain_level = (config.mode == EnvelopeMode::AHD) ? 0.f : sustain / 50.0f;
		const int32_t sustain_y = level_y(sustain_level);

		oled_canvas::Canvas& image = OLED::main;
		const int32_t selected_pos = std::distance(items.begin(), std::ranges::find(items, currentItem));
		selected_x_ = -1, selected_y_ = -1;

		drawSegment(start_x, 0.f, attack_x, 1.f, config.shape.attack);

		switch (config.mode) {
		case EnvelopeMode::AHD: {
			const float hold_x = round(attack_x + decay_width);
			// The decay gets the room the release would have had, so the line ends where the release knob says
			const float decay_end_x = round(hold_x + release / 50.0f * (end_x - hold_x));
			image.drawLine(attack_x, start_y, hold_x, start_y);
			drawSegment(hold_x, 1.f, decay_end_x, 0.f, config.shape.decay);
			image.drawLine(decay_end_x, base_y, end_x, base_y);

			drawDottedLine(attack_x, attack_x > start_x + 3, start_y);
			drawDottedLine(hold_x, hold_x - attack_x > 4, start_y);

			drawTransitionIndicator(attack_x, start_y, selected_pos == 0);
			drawTransitionIndicator(hold_x, start_y, selected_pos == 1);
			drawTransitionIndicator(decay_end_x, base_y, selected_pos == 3);
			break;
		}

		case EnvelopeMode::AR: {
			// Up to full while held (or to the level, with no pressure), down on release
			constexpr float sustain_x = start_x + max_segment_width * 3;
			const float release_x = round(sustain_x + release / 50.0f * (end_x - sustain_x));
			image.drawLine(attack_x, start_y, sustain_x, start_y);
			drawSegment(sustain_x, 1.f, release_x, 0.f, config.shape.release);
			image.drawLine(release_x, base_y, end_x, base_y);

			// The level the envelope rests at without pressure
			for (int32_t x = start_x; x <= sustain_x; x += 3) {
				image.drawPixel(x, sustain_y);
			}
			drawDottedLine(sustain_x, true, start_y);

			drawTransitionIndicator(attack_x, start_y, selected_pos == 0);
			drawTransitionIndicator(start_x + (sustain_x - start_x) / 2, sustain_y, selected_pos == 2);
			drawTransitionIndicator(release_x, base_y, selected_pos == 3);
			break;
		}

		default: {
			const float decay_x = round(attack_x + decay_width);
			constexpr float sustain_x = start_x + max_segment_width * 3; // Fixed sustainX position
			// Make release x dynamic, right of sustain
			const float release_x = round(sustain_x + release / 50.0f * (end_x - sustain_x));

			drawSegment(attack_x, 1.f, decay_x, sustain_level, config.shape.decay);
			image.drawLine(decay_x, sustain_y, sustain_x, sustain_y);
			drawSegment(sustain_x, sustain_level, release_x, 0.f, config.shape.release);
			image.drawLine(release_x, base_y, end_x, base_y);

			// reduce a messy look when lines are close to each other by omitting the line
			drawDottedLine(attack_x, attack_x > start_x + 3, start_y);
			drawDottedLine(decay_x, decay_x - attack_x > 4, start_y);
			drawDottedLine(sustain_x, true, sustain_y > start_y ? start_y : sustain_y);

			drawTransitionIndicator(attack_x, start_y, selected_pos == 0);
			drawTransitionIndicator(decay_x, sustain_y, selected_pos == 1);
			drawTransitionIndicator(decay_x + (sustain_x - decay_x) / 2, sustain_y, selected_pos == 2);
			drawTransitionIndicator(release_x, base_y, selected_pos == 3);
			break;
		}
		}
	}

private:
	int32_t envelope_index_;
	int32_t selected_x_, selected_y_;

	static constexpr int32_t kGraphTopY = OLED_MAIN_TOPMOST_PIXEL + kTextTitleSizeY + 7;
	static constexpr int32_t kGraphBaseY = OLED_MAIN_HEIGHT_PIXELS - 6;

	/// A stage from level `from` at x0 to level `to` at x1, bent like the envelope's curve
	static void drawSegment(float x0, float from, float x1, float to, const modulation::EnvelopeCurve& curve) {
		oled_canvas::Canvas& image = OLED::main;
		auto y = [](float level) {
			return static_cast<int32_t>(round(kGraphBaseY - level * (kGraphBaseY - kGraphTopY)));
		};
		constexpr int32_t kSteps = 12;
		int32_t last_x = static_cast<int32_t>(x0);
		int32_t last_y = y(from);
		for (int32_t i = 1; i <= kSteps; i++) {
			float t = static_cast<float>(i) / kSteps;
			int32_t this_x = static_cast<int32_t>(round(x0 + (x1 - x0) * t));
			int32_t this_y = y(from + (to - from) * curve.rising(t));
			image.drawLine(last_x, last_y, this_x, this_y);
			last_x = this_x;
			last_y = this_y;
		}
	}

	/// Vertical dotted line marking a stage transition, from the bottom of the screen up to `top_y`
	static void drawDottedLine(float x, bool draw, int32_t top_y) {
		if (!draw) {
			return;
		}
		for (int32_t y = OLED_MAIN_VISIBLE_HEIGHT + 1; y >= top_y - 2; y -= 4) {
			OLED::main.drawPixel(static_cast<int32_t>(x), y);
		}
	}

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
