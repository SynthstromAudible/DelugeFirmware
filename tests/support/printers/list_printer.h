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

#include <cstddef>
#include <ostream>
#include <span>

/// @file
/// @brief Prints a list field inside a record's `operator<<`: its elements in brackets, a space between each.
///
/// A record printer calls it field by field, `print_list(os, std::span{v.controls}) << ", arp ";`. Each element prints
/// through the `operator<<` argument-dependent lookup finds for its type, so a printer includes the header that
/// declares it.
namespace deluge::test::printers {

/// @brief Prints @p items as `[a b c]`, or `[]` where there are none. A byte-sized integer prints as a character, as
///        `std::ostream` prints one, so a record printer widens such a list itself.
/// @param os The stream to print to.
/// @param items The list.
/// @return @p os.
template <typename T, std::size_t N>
std::ostream& print_list(std::ostream& os, std::span<T, N> items) {
	os << '[';
	const char* separator = "";
	for (const T& item : items) {
		os << separator << item;
		separator = " ";
	}
	return os << ']';
}

} // namespace deluge::test::printers
