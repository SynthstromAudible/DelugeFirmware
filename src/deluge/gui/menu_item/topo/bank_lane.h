/*
 * Copyright © 2026 Synthstrom Audible Limited
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
#include "definitions_cxx.hpp"
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"
#include "model/clip/instrument_clip.h"
#include "util/string.h"

namespace deluge::gui::menu_item::topo {

/// Assigns one Topo Drums engine bank to a kit lane (note row). -1 = none; otherwise the
/// note-row index, displayed 1-based ("Row 3" is note row 2).
class BankLane final : public Integer {
public:
	BankLane(l10n::String newName, l10n::String newTitle, int32_t bank) : Integer(newName, newTitle), bank_{bank} {}

	[[nodiscard]] int32_t getMinValue() const override { return -1; }
	[[nodiscard]] int32_t getMaxValue() const override {
		InstrumentClip* clip = getCurrentInstrumentClip();
		return clip ? std::max<int32_t>(0, clip->noteRows.getNumElements() - 1) : 0;
	}

	void readCurrentValue() override {
		InstrumentClip* clip = getCurrentInstrumentClip();
		setValue(clip ? clip->topoDrumsSettings.bankRows[bank_] : -1);
	}

	void writeCurrentValue() override {
		InstrumentClip* clip = getCurrentInstrumentClip();
		if (!clip) {
			return;
		}
		int32_t value = getValue();
		clip->topoDrumsSettings.bankRows[bank_] =
		    (value >= 0 && value < clip->noteRows.getNumElements()) ? (int8_t)value : -1;
	}

	void drawPixelsForOled() override {
		if (getValue() < 0) {
			deluge::hid::display::OLED::main.drawStringCentred(l10n::get(l10n::String::STRING_FOR_NONE),
			                                                   18 + OLED_MAIN_TOPMOST_PIXEL, kTextHugeSpacingX,
			                                                   kTextHugeSizeY);
		}
		else {
			Integer::drawPixelsForOled();
		}
	}

	void drawValue() override {
		if (getValue() < 0) {
			display->setText(l10n::get(l10n::String::STRING_FOR_NONE));
		}
		else {
			Integer::drawValue();
		}
	}

	void getNotificationValue(StringBuf& value) override {
		if (getValue() < 0) {
			value.append(l10n::get(l10n::String::STRING_FOR_NONE));
		}
		else {
			value.append(l10n::get(l10n::String::STRING_FOR_ROW));
			value.append(' ');
			value.appendInt(getValue() + 1);
		}
	}

protected:
	[[nodiscard]] int32_t getDisplayValue() override { return getValue() + 1; } // rows shown 1-based

private:
	int32_t bank_;
};

} // namespace deluge::gui::menu_item::topo
