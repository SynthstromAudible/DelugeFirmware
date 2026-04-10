/*
 * Copyright (c) 2025 Nikodemus Siivola
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
#include "deluge/gui/menu_item/horizontal_menu.h"
#include "gui/menu_item/formatted_title.h"
#include "gui/menu_item/toggle.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::osc {
class Tracking final : public Toggle, public FormattedTitle {
public:
	using Toggle::Toggle;
	Tracking(l10n::String title_format_str, uint8_t source_id)
	    : Toggle(), FormattedTitle(title_format_str, source_id + 1), source_id_{source_id} {}
	void readCurrentValue() override { this->setValue(soundEditor.currentSound->sources[source_id_].isTracking); }
	void writeCurrentValue() override { soundEditor.currentSound->sources[source_id_].isTracking = this->getValue(); }

	[[nodiscard]] std::string_view getName() const override { return FormattedTitle::title(); }
	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	void getColumnLabel(StringBuf& label) override {
		label.append(getName());
		label.truncate(4);
	}

	void selectEncoderAction(int32_t offset) override {
		if (parent != nullptr && parent->renderingStyle() == Submenu::RenderingStyle::HORIZONTAL) {
			// reverse direction
			offset *= -1;
		}
		Toggle::selectEncoderAction(offset);
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		using namespace deluge::hid::display;
		const auto& icon = getValue() ? OLED::keyboardIcon : OLED::crossedOutKeyboardIcon;
		OLED::main.drawIconCentered(icon, slot.start_x, slot.width, slot.start_y - 1);
	}

private:
	uint8_t source_id_;
};

} // namespace deluge::gui::menu_item::osc
