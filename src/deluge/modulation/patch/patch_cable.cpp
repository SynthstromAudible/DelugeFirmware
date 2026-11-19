/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "modulation/patch/patch_cable.h"
#include "definitions_cxx.hpp"

#include <storage/flash_storage.h>

Polarity stringToPolarity(const std::string_view string) {
	return deluge::instrument::parse_polarity(string);
}
std::string_view polarityToString(const Polarity polarity) {
	return deluge::instrument::to_string(polarity);
}
std::string_view polarityToStringShort(const Polarity polarity) {
	return deluge::instrument::to_short_string(polarity);
}

bool PatchCable::hasPolarity(PatchSource source) {
	return deluge::instrument::has_polarity(source);
}

Polarity PatchCable::getDefaultPolarity(PatchSource source) {
	return deluge::instrument::fixed_polarity(source).value_or(FlashStorage::defaultPatchCablePolarity);
}

void PatchCable::setDefaultPolarity() {
	polarity = getDefaultPolarity(from);
}
