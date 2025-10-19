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

#include "dsp/reverb/reverb.hpp"
#include "gui/menu_item/integer.h"
#include "processing/engines/audio_engine.h"
#include <cmath>

namespace deluge::gui::menu_item::reverb {
class RoomSize final : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override { this->setValue(std::round(AudioEngine::reverb.getRoomSize() * kMaxMenuValue)); }
	void writeCurrentValue() override { AudioEngine::reverb.setRoomSize((float)this->getValue() / kMaxMenuValue); }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }

	[[nodiscard]] std::string_view getName() const override {
		using enum l10n::String;
		switch (AudioEngine::reverb.getModel()) {
		case dsp::Reverb::Model::DIGITAL:
		case dsp::Reverb::Model::MUTABLE:
			return l10n::getView(STRING_FOR_TIME);
		default:
			return l10n::getView(this->name);
		}
	}
	[[nodiscard]] std::string_view getTitle() const override { return getName(); }

	void renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) override {
		oled_canvas::Canvas& image = OLED::main;

		constexpr uint8_t rect_width = 21;
		constexpr uint8_t rect_height = 13;

		const uint8_t rect_start_x = startX + (width - rect_width) / 2;
		const uint8_t rect_end_x = rect_start_x + rect_width - 1;
		const uint8_t rect_start_y = startY + kHorizontalMenuSlotYOffset - 1;
		const uint8_t rect_end_y = rect_start_y + rect_height - 1;

		const float norm = getNormalizedValue();
		const uint8_t rect_effective_x = std::lerp(rect_start_x + 4, rect_end_x, norm);

		// Draw the main rectangle
		image.drawRectangle(rect_start_x, rect_start_y, rect_effective_x, rect_end_y);
		constexpr uint8_t inner_offset = 4;
		image.invertArea(rect_start_x + inner_offset, rect_effective_x - rect_start_x - inner_offset * 2 + 1,
		                 rect_start_y + inner_offset, rect_end_y - inner_offset);

		// Draw silhouette
		for (uint8_t x = rect_end_x; x > rect_effective_x + 2; x -= 3) {
			image.drawPixel(x, rect_start_y);
			image.drawPixel(x, rect_end_y);
		}
		for (uint8_t y = rect_start_y; y <= rect_end_y; y += 3) {
			image.drawPixel(rect_end_x, y);
		}
	}
};
} // namespace deluge::gui::menu_item::reverb
