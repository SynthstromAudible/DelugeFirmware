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

/// @file
/// @brief The DEVICE setting group: the device's own settings, which it keeps whatever song is loaded. Its rows are
///        the table `table_of(SettingGroupId::DEVICE)` gives; the host stores their values where the device keeps them
///        (the flash settings, the pad controller), and a menu reads and writes them through the scenes' device port.
///
/// A CHOICE row's value is its choice's index, so the scenes need no legacy type for it. The enums here exist only for
/// the rows whose value a scene branches on; each enumerator's value is its choice's index in the row.

#include "plugin-api/types/setting.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace deluge::project {

/// @brief The DEVICE group's rows.
/// @return Every row, in a table that lives for the program's life.
[[nodiscard]] std::span<const plugin::SettingInfo> device_table();

/// @brief Which of the two shortcut layouts the grid's parameter shortcuts resolve: the `pads/shortcutsVersion` row.
enum class ShortcutsVersion : uint8_t {
	V1 = 0, ///< The pre-3.0 layout.
	V3 = 1, ///< The current one, and the default.
};

/// @brief How a playhead is shown on the pads: the `pads/playCursor` row.
enum class FlashCursor : uint8_t {
	FAST = 0, ///< the pad controller flashes the pad; no pixel of the rendered image changes
	OFF = 1,  ///< no playhead
	SLOW = 2, ///< the factory default: the playhead's pixel is substituted in the rendered image
};

/// @brief The paths of the DEVICE rows the scenes name in code.
namespace device_path {
/// Whether a note's name uses a sharp or a flat: BOOL, 1 for sharps.
inline constexpr std::string_view kUseSharps = "defaults/useSharps";
/// The polarity a new patch cable takes: CHOICE over the patch cable's two polarities, unipolar then bipolar.
inline constexpr std::string_view kPatchCablePolarity = "defaults/patchCablePolarity";
/// The first part of each preset scale's "disabled by default" path; the scale's index follows it.
inline constexpr std::string_view kDisabledScalePrefix = "defaults/scales/disabled/";
/// The grid's shortcut layout: CHOICE over ShortcutsVersion.
inline constexpr std::string_view kShortcutsVersion = "pads/shortcutsVersion";
/// How a playhead is shown on the pads: CHOICE over FlashCursor.
inline constexpr std::string_view kPlayCursor = "pads/playCursor";
} // namespace device_path

} // namespace deluge::project
