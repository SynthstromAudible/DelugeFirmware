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
#include "model/settings/runtime_feature_settings.h"
#include "storage/flash_storage.h"
#include "util/lookuptables/lookuptables.h"

namespace deluge::gui::menu_item::defaults {
class Scale final : public Selection {
public:
	using Selection::Selection;

	void readCurrentValue() override {
		int32_t savedScale = FlashStorage::defaultScale;
		if (savedScale == PRESET_SCALE_RANDOM) {
			this->setValue(NUM_PRESET_SCALES);
		}
		else if (savedScale == PRESET_SCALE_NONE) {
			this->setValue(NUM_PRESET_SCALES + 1);
		}
		else {
			if (savedScale >= OFFSET_5_NOTE_SCALE) {
				// remove offset for 5 note scales
				savedScale = FIRST_5_NOTE_SCALE_INDEX + savedScale - OFFSET_5_NOTE_SCALE;
			}
			else if (savedScale >= OFFSET_6_NOTE_SCALE) {
				// remove offset for 6 note scales
				savedScale = FIRST_6_NOTE_SCALE_INDEX + savedScale - OFFSET_6_NOTE_SCALE;
			}
			if (savedScale >= NUM_PRESET_SCALES) {
				// Index is out of bounds, so reset to 0
				this->setValue(0);
			}
			else {
				// Otherwise set to the saved scale
				this->setValue(savedScale);
			}
		}
	}

	void writeCurrentValue() override {
		int32_t v = this->getValue();
		if (v == NUM_PRESET_SCALES) {
			FlashStorage::defaultScale = PRESET_SCALE_RANDOM;
		}
		else if (v == NUM_PRESET_SCALES + 1) {
			FlashStorage::defaultScale = PRESET_SCALE_NONE;
		}
		else {
			if (v >= FIRST_5_NOTE_SCALE_INDEX) {
				// apply offset to 5 note scales
				v = OFFSET_5_NOTE_SCALE + v - FIRST_5_NOTE_SCALE_INDEX;
			}
			else if (v >= FIRST_6_NOTE_SCALE_INDEX) {
				// apply offset to 6 note scales
				v = OFFSET_6_NOTE_SCALE + v - FIRST_6_NOTE_SCALE_INDEX;
			}
			FlashStorage::defaultScale = v;
		}
	}

	deluge::vector<std::string_view> getOptions() override {
		deluge::vector<std::string_view> scales = {presetScaleNames.begin(),
		                                           presetScaleNames.begin() + NUM_PRESET_SCALES};
		scales.push_back("RANDOM");
		scales.push_back("NONE");
		return scales;
	}
};
} // namespace deluge::gui::menu_item::defaults
