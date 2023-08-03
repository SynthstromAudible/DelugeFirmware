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
#include "definitions_cxx.hpp"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "storage/flash_storage.h"
#include "util/misc.h"

namespace menu_item::keyboard {
class Layout final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() { soundEditor.currentValue = util::to_underlying(FlashStorage::keyboardLayout); }
	void writeCurrentValue() { FlashStorage::keyboardLayout = static_cast<KeyboardLayout>(soundEditor.currentValue); }
	char const** getOptions() {
		static char const* options[] = {"QWERTY",                      // QWERTY
		                                "AZERTY",                      // AZERTY
		                                HAVE_OLED ? "QWERTZ" : "QRTZ", // QWERTZ
		                                nullptr};                      // Null-term
		return options;
	}
	int getNumOptions() { return kNumKeyboardLayouts; }
};
} // namespace menu_item::keyboard
