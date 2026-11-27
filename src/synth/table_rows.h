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

#include <tuple>
#include <type_traits>

/// @file
/// @brief Walks over a table of rows, a row being a type and a table a std::tuple of them: the synth's source and
/// engine tables are generated with these.

namespace deluge::synth {

/// @brief Calls @p fn with each row of @p Rows (a tuple of row types) as a value of that type, in order, until @p fn
/// returns true.
/// @param fn Called as `fn(Row{})`; true stops the walk.
/// @return Whether some call returned true.
template <class Rows, class Fn>
constexpr bool find_row(Fn fn) {
	return [&]<class... Row>(std::type_identity<std::tuple<Row...>>) {
		return (fn(Row{}) || ...);
	}(std::type_identity<Rows>{});
}

/// @brief Calls @p fn with each row of @p Rows, in order.
/// @param fn Called as `fn(Row{})` for each row.
template <class Rows, class Fn>
constexpr void for_each_row(Fn fn) {
	[&]<class... Row>(std::type_identity<std::tuple<Row...>>) { (fn(Row{}), ...); }(std::type_identity<Rows>{});
}

} // namespace deluge::synth
