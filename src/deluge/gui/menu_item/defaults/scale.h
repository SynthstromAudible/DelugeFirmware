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
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "storage/flash_storage.h"
#include "util/container/static_vector.hpp"
#include "util/lookuptables/lookuptables.h"

namespace deluge::gui::menu_item::defaults {
class Scale final : public Selection<NUM_PRESET_SCALES + 2> {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(FlashStorage::defaultScale); }
	void writeCurrentValue() override { FlashStorage::defaultScale = this->getValue(); }
	static_vector<std::string_view, capacity()> getOptions() override {
		return {presetScaleNames.begin(), presetScaleNames.begin() + capacity()};
	}
};
} // namespace deluge::gui::menu_item::defaults
