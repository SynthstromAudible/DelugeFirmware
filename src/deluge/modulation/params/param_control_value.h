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

#pragma once

#include "modulation/params/param_control.h"
#include <cstdint>

class AutoParamData;
class ClipParamValue;

namespace deluge::modulation::params {
// Scalar reads accept a null automation pointer: callers need not resolve AutoParamData.
int32_t readControlValue(ControlLayer layer, const ClipParamValue* scalar, const AutoParamData* automation,
                         int32_t missingValue = 0);
// Data operation used after the collection has recorded undo. Returns the previous effective value for notification.
// Neither the curve nor its interpolation/override state is changed.
int32_t setClipValueFromControl(ClipParamValue& scalar, const AutoParamData& automation, int32_t value);
} // namespace deluge::modulation::params
