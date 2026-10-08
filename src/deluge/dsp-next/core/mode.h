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
#include <utility>

/// @file
/// @brief Per-block mode dispatch: a runtime enum value selects a step instantiated for that value.

namespace deluge::dsp {

/// @brief The modes a kernel's step is instantiated for; with_mode dispatches over exactly these.
/// @tparam Ms The enumerators, all of enumeration type.
template <auto... Ms>
requires(sizeof...(Ms) > 0 && (std::is_enum_v<decltype(Ms)> && ...))
struct ModeList {};

namespace detail {
/// @brief Expands the list into one comparison per mode and calls the body for the match.
template <auto M0, auto... Ms, typename Mode, typename Body>
[[gnu::always_inline]] constexpr decltype(auto) with_mode_impl(ModeList<M0, Ms...> /*list*/, Mode mode, Body&& body) {
	using Result = std::remove_cvref_t<decltype(body.template operator()<M0>())>;
	if constexpr (std::is_void_v<Result>) {
		const bool matched = ((mode == M0 ? (body.template operator()<M0>(), true) : false) || ...
		                      || (mode == Ms ? (body.template operator()<Ms>(), true) : false));
		if (!matched) {
			std::unreachable();
		}
	}
	else {
		Result result{};
		const bool matched = ((mode == M0 ? (result = body.template operator()<M0>(), true) : false) || ...
		                      || (mode == Ms ? (result = body.template operator()<Ms>(), true) : false));
		if (!matched) {
			std::unreachable();
		}
		return result;
	}
}
} // namespace detail

/// @brief Calls @p body.template operator()<M>() for the mode M in @p List equal to @p mode — once, through a switch
/// the compiler builds from the list. A mode outside the list is a precondition violation (unreachable).
/// @tparam List A ModeList naming the modes to instantiate.
/// @param mode The runtime mode, chosen once per block.
/// @param body A callable with a templated call operator taking the mode as its sole non-type argument. Every
/// instantiation must return the same type.
/// @return Whatever @p body returns.
template <typename List, typename Mode, typename Body>
[[gnu::always_inline]] decltype(auto) with_mode(Mode mode, Body&& body) {
	return detail::with_mode_impl(List{}, mode, std::forward<Body>(body));
}

} // namespace deluge::dsp
