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
#include <ostream>

/// @file
/// @brief Prints an optional field inside a record's `operator<<`: its value, or `none` where it is empty.
///
/// A record printer calls it field by field, `print_optional(os, v.clip) << ", row ";`. The value prints through the
/// `operator<<` argument-dependent lookup finds for its type, so a printer includes the header that declares it.
namespace deluge::test::printers {

/// @brief Prints @p v, or `none` where it is empty.
/// @param os The stream to print to.
/// @param v The value.
/// @return @p os.
template <typename T>
std::ostream& print_optional(std::ostream& os, const std::optional<T>& v) {
	return v ? os << *v : os << "none";
}

} // namespace deluge::test::printers
