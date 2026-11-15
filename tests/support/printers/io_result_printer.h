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

#include "io/file.hpp"
#include "support/printers/expected_printer.h"

#include <expected>
#include <format>
#include <ostream>

/// @file
/// @brief Prints a directory entry and the results of the file layer's calls in a failed CppSpec comparison.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.

/// @brief Prints a directory entry as its name, whether it is a directory, and its size, e.g.
///        `{name "a.xml", directory 0, size 5}`.
/// @param os The stream to print to.
/// @param e The entry.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const DelugeDirEntry& e) {
	return os << std::format("{{name \"{}\", directory {}, size {}}}", e.name, int{e.is_directory}, e.size);
}

namespace deluge::io {

/// @brief Prints a file-layer result as its value, or its status as a number, e.g. `{value 5}` or `{error 8}`.
/// @param os The stream to print to.
/// @param e The result.
/// @return @p os.
template <typename T>
std::ostream& operator<<(std::ostream& os, const std::expected<T, Status>& e) {
	return deluge::test::printers::print_expected(os, e);
}

} // namespace deluge::io
