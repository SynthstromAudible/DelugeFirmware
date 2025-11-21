/*
 * Copyright (c) 2025 Synthstrom Audible Limited
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
#include "gui/l10n/l10n.h"
#include "gui/menu_item/unpatched_param.h"

namespace deluge::gui::menu_item::stutter {

class Rate final : public UnpatchedParam {
public:
	Rate(l10n::String newName, l10n::String title)
	    : UnpatchedParam(newName, title, deluge::modulation::params::UNPATCHED_STUTTER_RATE) {}

	void selectEncoderAction(int32_t offset) override;
	void drawValue() override;
	void drawPixelsForOled() override;
	void renderInHorizontalMenu(const SlotPosition& slot) override;
	void getNotificationValue(StringBuf& valueBuf) override;

	void getColumnLabel(StringBuf& label) override { label.append(deluge::l10n::get(l10n::String::STRING_FOR_RATE)); }

private:
	int32_t getClosestQuantizedOptionIndex(int32_t value) const;
	const char* getQuantizedOptionLabel();
	bool isStutterQuantized();
	static std::vector<const char*> optionLabels;
	static std::vector<int32_t> optionValues;
};

} // namespace deluge::gui::menu_item::stutter
