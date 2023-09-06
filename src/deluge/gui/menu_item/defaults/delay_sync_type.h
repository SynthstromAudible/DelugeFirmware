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
#include "util/misc.h"

namespace deluge::gui::menu_item::defaults {
class DelaySyncType final : public Selection<kNumSyncTypes> {
public:
	using Selection::Selection;
	void readCurrentValue() override {
		::SyncType type = FlashStorage::defaultDelaySyncType;
		if (type == SYNC_TYPE_EVEN) {
			this->setValue(0);
		}
		else if (type == SYNC_TYPE_TRIPLET) {
			this->setValue(1);
		}
		else if (type == SYNC_TYPE_DOTTED) {
			this->setValue(2);
		}
	}
	void writeCurrentValue() override {
		int32_t value = this->getValue();
		if (value == 0) {
			FlashStorage::defaultDelaySyncType = SYNC_TYPE_EVEN;
		}
		else if (value == 1) {
			FlashStorage::defaultDelaySyncType = SYNC_TYPE_TRIPLET;
		}
		else if (value == 2) {
			FlashStorage::defaultDelaySyncType = SYNC_TYPE_DOTTED;
		}
	}
	static_vector<std::string_view, capacity()> getOptions() override {
		return {
		    l10n::getView(l10n::String::STRING_FOR_SYNC_TYPE_EVEN),
		    l10n::getView(l10n::String::STRING_FOR_SYNC_TYPE_TRIPLET),
		    l10n::getView(l10n::String::STRING_FOR_SYNC_TYPE_DOTTED),
		};
	}
};
} // namespace deluge::gui::menu_item::defaults
