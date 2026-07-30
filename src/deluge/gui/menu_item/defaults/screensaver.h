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
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "gui/menu_item/integer.h"
#include "gui/menu_item/selection.h"
#include "gui/menu_item/submenu.h"
#include "hid/display/display.h"
#include "hid/display/screensaver.h"
#include "storage/flash_storage.h"

namespace deluge::gui::menu_item::defaults {

/// @brief Picks what the screensaver shows: off, blank, or the starscape.
///
/// @note Suffixed ...Menu to avoid colliding with the global ScreensaverMode enum.
class ScreensaverModeMenu final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(FlashStorage::screensaverMode); }
	void writeCurrentValue() override {
		FlashStorage::screensaverMode = this->getValue<::ScreensaverMode>();
		hid::display::Screensaver::settingsChanged();
	}
	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		return {l10n::getView(l10n::String::STRING_FOR_OFF),
		        l10n::getView(l10n::String::STRING_FOR_SCREENSAVER_MODE_BLANK),
		        l10n::getView(l10n::String::STRING_FOR_SCREENSAVER_MODE_STARSCAPE)};
	}
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override { return display->haveOLED(); }
};

/// @brief Sets the number of minutes without physical input before the screensaver appears.
class ScreensaverTimeout final : public Integer {
public:
	using Integer::Integer;
	[[nodiscard]] int32_t getMinValue() const override { return 1; }
	[[nodiscard]] int32_t getMaxValue() const override { return 60; }
	void readCurrentValue() override { this->setValue(FlashStorage::screensaverTimeoutMinutes); }
	void writeCurrentValue() override {
		FlashStorage::screensaverTimeoutMinutes = this->getValue();
		hid::display::Screensaver::settingsChanged();
	}
	const char* getUnit() override { return " MIN"; }
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override { return display->haveOLED(); }
};

/// @brief Holds the screensaver settings, and hides the whole submenu on 7SEG units rather than
///        showing an empty one.
class ScreensaverSubmenu final : public Submenu {
public:
	using Submenu::Submenu;
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override { return display->haveOLED(); }
};

} // namespace deluge::gui::menu_item::defaults
