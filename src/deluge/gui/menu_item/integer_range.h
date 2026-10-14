/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "range.h"
#include "storage/flash_storage.h"

namespace deluge::gui::menu_item {

class IntegerRange final : public Range {
public:
	/// @param slot Which `FlashStorage::defaultRanges` entry this item's `lower`/`upper` name. The
	///             two values used to be members here; they are settings the loader has always
	///             persisted, so they live in `FlashStorage` and this binds references onto them --
	///             the same move `menu_item::Colour::value` made, and for the same reason (a layer
	///             that may not name `gui/` still has to edit them). See
	///             `storage/flash_storage.h`'s `defaultRanges`.
	IntegerRange(l10n::String newName, l10n::String title, int32_t newMin, int32_t newMax,
	             FlashStorage::DefaultRangeSlot slot)
	    : Range(newName, title), lower(FlashStorage::defaultRanges[slot].lower),
	      upper(FlashStorage::defaultRanges[slot].upper), minValue(newMin), maxValue(newMax) {}
	void beginSession(MenuItem* navigatedBackwardFrom) override;
	void getText(char* buffer, int32_t* getLeftLength, int32_t* getRightLength, bool mayShowJustOne) override;
	void selectEncoderAction(int32_t offset) override;
	int32_t getRandomValueInRange();

	int32_t& lower;
	int32_t& upper;

	int32_t minValue, maxValue;
};
} // namespace deluge::gui::menu_item
