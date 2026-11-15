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

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <ostream>
#include <span>
#include <utility>

/// @file
/// @brief Prints a list field inside a record's `operator<<`: its elements in brackets, a space between each, an
///        array of enums as their numbers, or a map of records by id.
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

/// @brief Prints an array of enums as their numbers in brackets, e.g. `[0 0 2 1]`.
/// @param os The stream to print to.
/// @param values The enums.
/// @return @p os.
template <typename E, std::size_t N>
std::ostream& print_enums(std::ostream& os, const std::array<E, N>& values) {
	os << '[';
	for (std::size_t i = 0; i < N; ++i) {
		os << (i == 0 ? "" : " ") << +std::to_underlying(values[i]);
	}
	return os << ']';
}

/// @brief Prints a map of records by id in braces, e.g. `{1: {…}, 4: {…}}`.
/// @param os The stream to print to.
/// @param records The records.
/// @return @p os.
template <typename T>
std::ostream& print_by_id(std::ostream& os, const std::map<uint32_t, T>& records) {
	os << '{';
	const char* separator = "";
	for (const auto& [id, record] : records) {
		os << separator << id << ": " << record;
		separator = ", ";
	}
	return os << '}';
}

} // namespace deluge::test::printers
