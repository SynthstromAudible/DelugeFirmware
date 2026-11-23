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

#include <concepts>
#include <type_traits>
#include <utility>

/// @file
/// @brief Per-block mode dispatch: a runtime mode value selects a step instantiated for that value.

namespace deluge::dsp {

/// @brief Whether @p T can be a mode: an enumeration, or a class compared with `==`, such as a chain's mode, which
/// holds one enumerator (or one nested mode) per moded part.
/// @details A mode is a template argument, so a class mode must also be structural: public, non-mutable members of
/// structural type. Its `==` must compare every member, since with_mode picks a step by it; a defaulted `==` does.
template <typename T>
concept mode_type = std::is_enum_v<T> || (std::is_class_v<T> && std::equality_comparable<T>);

/// @brief The modes a kernel's step is instantiated for; with_mode dispatches over exactly these.
/// @tparam Ms The modes, each a @ref mode_type value.
template <auto... Ms>
requires(sizeof...(Ms) > 0 && (mode_type<decltype(Ms)> && ...))
struct ModeList {};

namespace detail {
/// @brief Compares @p mode with each listed mode in turn and returns the matching call's result, reference included.
/// @details The last mode is the fallthrough: a mode outside the list is unreachable. The return type is deduced
/// across all the calls, so a body whose instantiations disagree on it does not compile.
template <auto M0, auto... Ms, typename Mode, typename Body>
[[gnu::always_inline]] constexpr decltype(auto) with_mode_impl(ModeList<M0, Ms...> /*list*/, Mode mode, Body& body) {
	if constexpr (sizeof...(Ms) == 0) {
		if (mode != M0) {
			std::unreachable();
		}
		return body.template operator()<M0>();
	}
	else {
		if (mode == M0) {
			return body.template operator()<M0>();
		}
		return with_mode_impl(ModeList<Ms...>{}, mode, body);
	}
}
} // namespace detail

/// @brief Calls @p body.template operator()<M>() for the mode M in @p List equal to @p mode — once, through a chain of
/// comparisons against the list's modes in order. A mode outside the list is a precondition violation (unreachable).
/// @tparam List A ModeList naming the modes to instantiate.
/// @param mode The runtime mode, chosen once per block.
/// @param body A callable with a templated call operator taking the mode as its sole non-type argument. Every
/// instantiation must return the same type, which deduction enforces.
/// @return Whatever @p body returns, forwarded as it is returned (a reference stays a reference).
template <typename List, typename Mode, typename Body>
[[gnu::always_inline]] decltype(auto) with_mode(Mode mode, Body&& body) {
	return detail::with_mode_impl(List{}, mode, body);
}

} // namespace deluge::dsp
