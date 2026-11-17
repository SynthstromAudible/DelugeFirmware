/*
 * Copyright (c) 2026 Synthstrom Audible Limited
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
#include "gui/l10n/l10n.h"
#include "gui/menu_item/selection.h"
#include "model/song/song.h"
#include "playback/swing_map.h"
#include "storage/flash_storage.h"

namespace deluge::gui::menu_item::swing {

/// How finer swing levels follow the primary swing amount, for the current song or as the default for new songs.
class Feel final : public Selection {
public:
	enum Kind { SONG, DEFAULT };
	Feel(l10n::String newName, Kind kind_) : Selection(newName), kind(kind_) {}

	void readCurrentValue() override {
		if (kind == DEFAULT) {
			this->setValue(FlashStorage::defaultSwingFeel);
		}
		else if (currentSong) {
			this->setValue(currentSong->swingFeel);
		}
	}

	void writeCurrentValue() override {
		if (kind == DEFAULT) {
			FlashStorage::defaultSwingFeel = this->getValue<SwingFeel>();
		}
		else if (currentSong) {
			currentSong->swingFeel = this->getValue<SwingFeel>();
		}
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		static_assert(kNumSwingFeels == 3);
		return {
		    l10n::getView(STRING_FOR_SWING_FEEL_CLASSIC),
		    l10n::getView(STRING_FOR_SWING_FEEL_NESTED),
		    l10n::getView(STRING_FOR_SWING_FEEL_EVEN),
		};
	}

private:
	Kind kind;
};

} // namespace deluge::gui::menu_item::swing
