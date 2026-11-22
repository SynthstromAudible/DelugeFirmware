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

#include <optional>
#include <string_view>

namespace deluge::project {

/// @brief How a session clip launches. Saved as a clip's `launchStyle` attribute.
enum class LaunchStyle {
	DEFAULT, ///< launches and loops like any clip
	FILL,    ///< plays once, ending at the launch point of the next section
	ONCE,    ///< plays through once, then stops
};

/// @brief The `launchStyle` attribute text for @p style.
/// @return A view of a string literal, so `.data()` is null-terminated.
[[nodiscard]] constexpr std::string_view to_string(LaunchStyle style) noexcept {
	switch (style) {
	case LaunchStyle::DEFAULT:
		return "default";
	case LaunchStyle::FILL:
		return "fill";
	case LaunchStyle::ONCE:
		return "once";
	}
	return "default";
}

/// @brief The style a `launchStyle` attribute names.
/// @return The style, or empty for text that names none; songs read empty as `LaunchStyle::DEFAULT`.
[[nodiscard]] constexpr std::optional<LaunchStyle> parse_launch_style(std::string_view text) noexcept {
	for (LaunchStyle style : {LaunchStyle::DEFAULT, LaunchStyle::FILL, LaunchStyle::ONCE}) {
		if (text == to_string(style)) {
			return style;
		}
	}
	return std::nullopt;
}

} // namespace deluge::project
