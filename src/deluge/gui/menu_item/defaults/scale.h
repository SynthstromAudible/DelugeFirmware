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
#include "model/scale/preset_scales.h"
#include "storage/flash_storage.h"

namespace deluge::gui::menu_item::defaults {
class DefaultScale final : public Selection {
public:
	using Selection::Selection;

	void readCurrentValue() override {
		this->setValue(scaleToOptionIndex(flashStorageCodeToScale(FlashStorage::defaultScale)));
	}

	void writeCurrentValue() override {
		FlashStorage::defaultScale = scaleToFlashStorageCode(optionIndexToScale(static_cast<Scale>(this->getValue())));
	}

	void drawName() override { display->setScrollingText(getName().data()); }

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		deluge::vector<std::string_view> scales;
		for (uint8_t i = 0; i < NUM_SCALELIKE; i++) {
			if (i != USER_SCALE) {
				scales.push_back(scalelikeNames[i]);
			}
		}
		return scales;
	}

private:
	// USER_SCALE is not available as a default, since it's not saved to flash storage,
	// so we need to offset the index.
	Scale optionIndexToScale(uint8_t index) {
		if (index >= USER_SCALE) {
			index += 1;
		}
		return static_cast<Scale>(index);
	}
	uint8_t scaleToOptionIndex(Scale scale) {
		if (scale >= USER_SCALE) {
			return scale - 1;
		}
		else {
			return scale;
		}
	}
};
} // namespace deluge::gui::menu_item::defaults
