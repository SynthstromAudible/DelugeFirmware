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

#include "deluge-project/param_automation.h"
#include "serialization/value.h"

#include <cstdint>

namespace deluge::bridge {

/// @brief An automation leaf (a cable's `amount`) read as `AutoParam::readFromFile` reads it: the `0x…` value and
/// any nodes up to @p readAutomationUpToPos.
[[nodiscard]] deluge::project::DocAutoParam amountOf(const deluge::serialization::Value& leaf,
                                                     int32_t readAutomationUpToPos);

/// @brief Whether an automation leaf holds something: a non-zero value or automation (`AutoParam::containsSomething`
/// with a neutral value of 0), read up to @p readAutomationUpToPos. The test the Deluge instrument's cable decoder is
/// given.
[[nodiscard]] bool containsSomething(const deluge::serialization::Value& leaf,
                                     int32_t readAutomationUpToPos = 2147483647);

} // namespace deluge::bridge
