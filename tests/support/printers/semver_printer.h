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

#include "serialization/semver.h"
#include "support/printers/expected_printer.h"

#include <expected>
#include <format>
#include <ostream>

/// @file
/// @brief Prints a semantic version and a parse's result in a failed CppSpec comparison.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.

/// @brief Prints a version as `major.minor.patch`, then `-` and its pre-release tag where it has one, e.g.
///        `1.5.2-alpha`.
/// @param os The stream to print to.
/// @param v The version.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const SemVer& v) {
	os << std::format("{}.{}.{}", v.major, v.minor, v.patch);
	return v.pre_release.empty() ? os : os << '-' << v.pre_release;
}

/// @brief Prints a parse's result as its version, or its parser error as a number, e.g. `{value 1.2.4}`.
/// @param os The stream to print to.
/// @param e The result.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const std::expected<SemVer, SemVer::Parser::Error>& e) {
	return deluge::test::printers::print_expected(os, e);
}
