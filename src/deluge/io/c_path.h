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

#include <libdeluge/channel.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

namespace deluge::io {

/// A path as a C function takes it: NUL-terminated, and at most `DELUGE_PATH_MAX` bytes with the NUL.
///
/// A `std::string_view` carries no terminator, so its `data()` must never go to a function that reads up to one.
/// Make a CPath from it instead. A path that does not fit is refused, never truncated: a truncated path names
/// another file, which may exist.
class CPath {
public:
	[[nodiscard]] static std::optional<CPath> from(std::string_view path) noexcept {
		if (path.size() >= DELUGE_PATH_MAX || path.find('\0') != std::string_view::npos) {
			return std::nullopt;
		}
		CPath out;
		std::ranges::copy(path, out.bytes_.begin());
		out.bytes_[path.size()] = '\0';
		out.size_ = path.size();
		return out;
	}

	[[nodiscard]] const char* c_str() const noexcept { return bytes_.data(); }
	[[nodiscard]] std::string_view view() const noexcept { return {bytes_.data(), size_}; }

private:
	CPath() = default;
	std::array<char, DELUGE_PATH_MAX> bytes_{};
	std::size_t size_ = 0;
};

} // namespace deluge::io
