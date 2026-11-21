/*
 * Copyright © 2025 Synthstrom Audible Limited
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

#include "modulation/params/param_value.h"

// Out of line so every caller shares this one internal-RAM copy. An inline definition is
// emitted into each user, and GUI objects, whose code lives in SDRAM, could provide the copy.
int32_t ClipParamValue::getAdjustedValue() const {
	int32_t preset = getPresetValue();
	if ((presetAndOffset_ & 2) != 0) [[unlikely]] {
		return resolve(int64_t{preset} + getOffset());
	}
	return preset == presetAtEdit_ ? valueAtEdit_ : resolve(int64_t{preset} + getOffset());
}
