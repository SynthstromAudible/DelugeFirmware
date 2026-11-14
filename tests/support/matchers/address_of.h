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

#include <type_traits>

/// @file
/// @brief A function pointer as an address, for comparing two of them in a CppSpec expectation.
///
/// `std::ostream` has no `operator<<` for a function pointer, so CppSpec prints one through its conversion to `bool`
/// and a failed identity check reads `expected 1 to equal 1`. Compared as `const void*`, a failure prints both
/// addresses: `expect(address_of(node.bind.get)).to_equal(address_of(&get_level));`.
namespace deluge::test::matchers {

/// @brief @p fn as an address, so a failed comparison prints where it points rather than `1`.
/// @param fn The function pointer.
/// @return The address @p fn points at.
template <typename F>
requires std::is_function_v<F>
const void* address_of(F* fn) {
	return reinterpret_cast<const void*>(fn);
}

} // namespace deluge::test::matchers
