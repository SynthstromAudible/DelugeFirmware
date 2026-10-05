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

#include "engine-api/types/setting.h"
#include "serialization/value.h"

#include <span>
#include <string_view>

namespace deluge::instrument {

/// @brief The Deluge instrument's non-param settings, by their path in a sound's element: the scalars a setting Edit
/// changes, and the structured ones (sample and wavetable ranges, the DX7 patch) only their own Edits change.
extern const std::span<const engine::SettingInfo> kSettings;

/// @brief Whether @p value is one @p path's row allows: a non-structured row's choice, an integer in its range, or 0
/// or 1 for a bool. False for a structured row or a path the schema does not hold.
[[nodiscard]] bool check_setting(std::string_view path, const serialization::Value& value);

} // namespace deluge::instrument
