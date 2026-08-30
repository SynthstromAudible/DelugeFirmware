/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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
#include "storage/flash_storage.h"

namespace deluge::gui::menu_item {

constexpr int32_t kNumPadColours = 9;
class Colour final : public Selection {
public:
	enum Option : uint8_t {
		RED,
		GREEN,
		BLUE,
		YELLOW,
		CYAN,
		MAGENTA,
		AMBER,
		WHITE,
		PINK,
	};

	/// @param slot Which of `FlashStorage::padColours` this item edits -- see the note on `value`.
	Colour(l10n::String newName, FlashStorage::PadColourSlot slot)
	    : Selection(newName), value(FlashStorage::padColours[slot]) {}

	void readCurrentValue() override { this->setValue(value); }
	void writeCurrentValue() override {
		value = static_cast<Option>(this->getValue());
		renderingNeededRegardlessOfUI();
	};
	deluge::vector<std::string_view> getOptions(OptType optType) override {
		return {
		    l10n::getView(l10n::String::STRING_FOR_RED),   l10n::getView(l10n::String::STRING_FOR_GREEN),
		    l10n::getView(l10n::String::STRING_FOR_BLUE),  l10n::getView(l10n::String::STRING_FOR_YELLOW),
		    l10n::getView(l10n::String::STRING_FOR_CYAN),  l10n::getView(l10n::String::STRING_FOR_MAGENTA),
		    l10n::getView(l10n::String::STRING_FOR_AMBER), l10n::getView(l10n::String::STRING_FOR_WHITE),
		    l10n::getView(l10n::String::STRING_FOR_PINK),
		};
	}
	[[nodiscard]] RGB getRGB() const;

	/// @brief This item's colour, as an `Option`.
	///
	/// A **reference onto `FlashStorage::padColours`**, not a member of its own: the byte is a
	/// persisted setting that `flash_storage.cpp` and `src/scenes`'s port of this page both address,
	/// and neither can reach a member of a `gui/` class. Every legacy spelling
	/// (`activeColourMenu.value = Colour::GREEN`, `buffer[74] = activeColourMenu.value`) is
	/// unchanged; it now reads and writes the shared byte.
	uint8_t& value;
};

extern Colour activeColourMenu;
extern Colour stoppedColourMenu;
extern Colour mutedColourMenu;
extern Colour soloColourMenu;
extern Colour fillColourMenu;
extern Colour onceColourMenu;
} // namespace deluge::gui::menu_item
