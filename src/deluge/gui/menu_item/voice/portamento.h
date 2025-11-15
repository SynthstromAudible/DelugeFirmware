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
#include "deluge/modulation/params/param.h"
#include "gui/menu_item/unpatched_param.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"

namespace deluge::gui::menu_item::voice {
class Portamento final : public UnpatchedParam {
public:
	using UnpatchedParam::UnpatchedParam;

	Portamento(l10n::String newName) : UnpatchedParam(newName, deluge::modulation::params::UNPATCHED_PORTAMENTO) {}

	void configureRenderingOptions(const HorizontalMenuRenderingOptions& options) override {
		UnpatchedParam::configureRenderingOptions(options);
		options.label = deluge::l10n::get(l10n::String::STRING_FOR_PORTAMENTO_SHORT);
	}

	void renderInHorizontalMenu(const HorizontalMenuSlotPosition& slot) override {
		using namespace deluge::hid::display;
		oled_canvas::Canvas& image = OLED::main;

		constexpr uint8_t porta_graphics_width = 25;
		const uint8_t center_x = slot.start_x + slot.width / 2;
		const uint8_t porta_start_x = slot.start_x + 2;
		const uint8_t porta_end_x = porta_start_x + porta_graphics_width - 1;

		constexpr uint8_t porta_graphics_height = 11;
		const uint8_t porta_start_y = slot.start_y + kHorizontalMenuSlotYOffset - 1;
		const uint8_t porta_end_y = porta_start_y + porta_graphics_height - 1;

		const float norm = normalize(getValue());
		constexpr uint8_t porta_line_width = 9;
		const uint8_t porta_x0 = std::lerp(center_x, center_x - porta_line_width, norm);
		const uint8_t porta_x1 = std::lerp(center_x, center_x + porta_line_width, norm);

		if (porta_x0 == porta_x1) {
			for (uint8_t y = porta_start_y; y <= porta_end_y; y += 2) {
				image.drawPixel(porta_x0, y);
			}
		}
		else {
			image.drawLine(porta_x0, porta_end_y, porta_x1, porta_start_y);
		}

		constexpr uint8_t max_note_width = 6;
		constexpr uint8_t note_offset = 2;
		const uint8_t lower_note_start_x = std::max<uint8_t>(porta_start_x, porta_x0 - note_offset - max_note_width);
		const uint8_t higher_note_end_x = std::min<uint8_t>(porta_end_x, porta_x1 + note_offset + max_note_width);

		image.drawHorizontalLine(porta_end_y, lower_note_start_x, porta_x0 - note_offset);
		image.drawHorizontalLine(porta_end_y - 1, lower_note_start_x, porta_x0 - note_offset);
		image.drawHorizontalLine(porta_start_y, porta_x1 + note_offset, higher_note_end_x);
		image.drawHorizontalLine(porta_start_y + 1, porta_x1 + note_offset, higher_note_end_x);
	}
};

} // namespace deluge::gui::menu_item::voice
