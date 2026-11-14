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

#include "support/printers/optional_printer.h"

#include <cstddef>
#include <expected>
#include <optional>
#include <ostream>
#include <span>
#include <type_traits>
#include <utility>

/// @file
/// @brief Prints a `std::expected` inside a result printer's `operator<<`: its value, or its error.
///
/// `std::expected` has no `operator<<`, and one declared for it alone would sit in `std`, where no code may add one.
/// A printer for a result type declares the operator in its error type's namespace, which argument-dependent lookup
/// searches for `std::expected<T, E>`, and forwards to print_expected().
namespace deluge::test::printers {

namespace detail {
template <typename T>
inline constexpr bool is_optional_v = false;
template <typename T>
inline constexpr bool is_optional_v<std::optional<T>> = true;
} // namespace detail

/// @brief Prints one side of a result: an enum as its number, an optional through print_optional(), a byte span as
///        its length, anything else through the `operator<<` argument-dependent lookup finds for it.
/// @param os The stream to print to.
/// @param v The value or error.
/// @return @p os.
template <typename T>
std::ostream& print_result_part(std::ostream& os, const T& v) {
	if constexpr (std::is_enum_v<T>) {
		return os << +std::to_underlying(v);
	}
	else if constexpr (std::is_same_v<T, std::span<std::byte>> || std::is_same_v<T, std::span<const std::byte>>) {
		return os << v.size() << " bytes";
	}
	else if constexpr (detail::is_optional_v<T>) {
		return print_optional(os, v);
	}
	else {
		return os << v;
	}
}

/// @brief Prints @p e as `{value V}`, `{value}` for a result that carries none, or `{error E}`, e.g. `{error 8}`.
/// @param os The stream to print to.
/// @param e The result.
/// @return @p os.
template <typename T, typename E>
std::ostream& print_expected(std::ostream& os, const std::expected<T, E>& e) {
	if (!e) {
		return print_result_part(os << "{error ", e.error()) << "}";
	}
	if constexpr (std::is_void_v<T>) {
		return os << "{value}";
	}
	else {
		return print_result_part(os << "{value ", *e) << "}";
	}
}

} // namespace deluge::test::printers
