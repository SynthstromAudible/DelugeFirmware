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
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "storage/flash_storage.h"
#include "util/lookuptables/lookuptables.h"
#include "util/misc.h"

namespace deluge::gui::menu_item::defaults {
class ReverbRoomPreset final : public Selection<NUM_PRESET_REVERBS> {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(FlashStorage::defaultReverbRoomPreset); }
	void writeCurrentValue() override { FlashStorage::defaultReverbRoomPreset = this->getValue(); }
	static_vector<std::string_view, capacity()> getOptions() override {
		return {
		    l10n::getView(l10n::String::STRING_FOR_SMALL_ROOM_REVERB),
		    l10n::getView(l10n::String::STRING_FOR_MEDIUM_ROOM_REVERB),
		    l10n::getView(l10n::String::STRING_FOR_LARGE_ROOM_REVERB),
		};
	}
};
} // namespace deluge::gui::menu_item::defaults
