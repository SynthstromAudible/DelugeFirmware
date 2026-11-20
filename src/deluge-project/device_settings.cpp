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

#include "deluge-project/device_settings.h"

#include "music-theory/preset_scales.h"

#include <array>
#include <cstddef>
#include <utility>

namespace deluge::project {

namespace {
/// The menu-time tables: read when a menu looks a setting up, never by the audio tier or an interrupt. The device
/// image places this namespace's data in SDRAM (linker/sdram_sections.x matches its name).
inline namespace menu_tables {

using plugin::SettingInfo;
using plugin::SettingKind;

/// How many preset scales a "disabled by default" row is kept for.
constexpr size_t kScaleCount = music::NUM_PRESET_SCALES;
static_assert(kScaleCount <= 100, "a scale's index is written as at most two digits");

/// One preset scale's "disabled by default" path, as text the table's views point into.
struct ScalePath {
	std::array<char, device_path::kDisabledScalePrefix.size() + 2> text{}; ///< the prefix, then the index's digits
	size_t size{0};                                                        ///< how many characters of `text` it is
};

/// Every preset scale's path, built when the image is: `defaults/scales/disabled/<i>` for each scale's index.
constexpr std::array<ScalePath, kScaleCount> kScalePaths = [] {
	std::array<ScalePath, kScaleCount> paths{};
	for (size_t scale = 0; scale < kScaleCount; ++scale) {
		ScalePath& path = paths[scale];
		for (const char c : device_path::kDisabledScalePrefix) {
			path.text[path.size++] = c;
		}
		if (scale >= 10) {
			path.text[path.size++] = static_cast<char>('0' + (scale / 10));
		}
		path.text[path.size++] = static_cast<char>('0' + (scale % 10));
	}
	return paths;
}();

/// The patch cable's polarities, as the cable's own file words name them (`instrument::to_string`).
constexpr std::array<std::string_view, 2> kPolarityWords{"unipolar", "bipolar"};
/// The shortcut layouts, in ShortcutsVersion's order.
constexpr std::array<std::string_view, 2> kShortcutsVersionWords{"v1", "v3"};
/// The playhead's forms, in FlashCursor's order.
constexpr std::array<std::string_view, 3> kPlayCursorWords{"fast", "off", "slow"};
static_assert(std::to_underlying(ShortcutsVersion::V3) == 1 && std::to_underlying(FlashCursor::SLOW) == 2,
              "an enumerator's value is its choice's index");

constexpr SettingInfo boolean(std::string_view path, int32_t default_value) {
	return SettingInfo{.path = path, .kind = SettingKind::BOOL, .default_value = default_value};
}

constexpr SettingInfo choice(std::string_view path, std::span<const std::string_view> choices, int32_t default_value) {
	return SettingInfo{.path = path, .kind = SettingKind::CHOICE, .choices = choices, .default_value = default_value};
}

/// The rows before the preset scales'.
constexpr size_t kLeadingRows = 2;
/// The rows after them.
constexpr size_t kTrailingRows = 2;

/// The DEVICE table: each row's default is the one the device's settings reset to.
constexpr std::array<SettingInfo, kLeadingRows + kScaleCount + kTrailingRows> kDeviceTable = [] {
	std::array<SettingInfo, kLeadingRows + kScaleCount + kTrailingRows> rows{};
	size_t at = 0;
	rows[at++] = boolean(device_path::kUseSharps, 1);
	rows[at++] = choice(device_path::kPatchCablePolarity, kPolarityWords, 1);
	for (const ScalePath& path : kScalePaths) {
		rows[at++] = boolean(std::string_view{path.text.data(), path.size}, 0);
	}
	rows[at++] =
	    choice(device_path::kShortcutsVersion, kShortcutsVersionWords, std::to_underlying(ShortcutsVersion::V3));
	rows[at++] = choice(device_path::kPlayCursor, kPlayCursorWords, std::to_underlying(FlashCursor::SLOW));
	return rows;
}();
static_assert(plugin::conditions_are_valid(kDeviceTable));
static_assert(plugin::mappings_are_valid(kDeviceTable));

} // namespace menu_tables
} // namespace

std::span<const plugin::SettingInfo> device_table() {
	return kDeviceTable;
}

} // namespace deluge::project
