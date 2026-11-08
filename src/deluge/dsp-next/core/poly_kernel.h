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

#include "dsp-next/core/kernel.h"
#include "dsp-next/core/pack_f32.h"
#include "dsp-next/core/sample.h"

#include <concepts>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

/// @file
/// @brief poly_kernel: a float processor kernel written once over its value type, so Poly and Bank can step a copy
/// per lane with each lane's own coefficients; set_lane and get_lane move one lane between a packed struct and a
/// scalar one.
namespace deluge::dsp {

namespace detail {
/// Whether @p T declares `fields`, a tuple of member pointers.
template <class T>
concept declares_fields = requires { std::tuple_size_v<std::remove_cvref_t<decltype(T::fields)>>; };
} // namespace detail

/// @brief A float processor kernel whose coefficients and state are written over V (f32 or Pack<f32, N>), with
/// `fields` naming each V member, and whose step is a template over V.
/// @details `Coefficients` is `CoefficientsT<f32>` and `State` is `StateT<f32>`, as Mono and Linked read them. A
/// coefficient member that is not a V (the mode) is not a field: every lane of one Poly or Bank shares it. A `prepare`
/// the kernel declares is a template over V, so it runs on a packed state as on a scalar one.
template <class K>
concept poly_kernel =
    processor_kernel<K> && std::same_as<typename K::value_type, f32>
    && std::same_as<typename K::Coefficients, typename K::template CoefficientsT<f32>>
    && std::same_as<typename K::State, typename K::template StateT<f32>>
    && detail::declares_fields<typename K::template CoefficientsT<f32>>
    && detail::declares_fields<typename K::template CoefficientsT<Pack<f32, 4>>>
    && detail::declares_fields<typename K::template StateT<f32>>
    && detail::declares_fields<typename K::template StateT<Pack<f32, 4>>> && !detail::declares_context<K>;

namespace detail {
template <class S, class P, std::size_t... I>
void set_lane_at(P& packed, std::size_t lane, const S& scalar, std::index_sequence<I...> /*fields*/) {
	static_assert(std::tuple_size_v<decltype(S::fields)> == std::tuple_size_v<decltype(P::fields)>,
	              "the scalar and packed structs name the same fields");
	((packed.*std::get<I>(P::fields)).set(lane, scalar.*std::get<I>(S::fields)), ...);
}

template <class S, class P, std::size_t... I>
void get_lane_at(const P& packed, std::size_t lane, S& scalar, std::index_sequence<I...> /*fields*/) {
	static_assert(std::tuple_size_v<decltype(S::fields)> == std::tuple_size_v<decltype(P::fields)>,
	              "the scalar and packed structs name the same fields");
	((scalar.*std::get<I>(S::fields) = (packed.*std::get<I>(P::fields)).get(lane)), ...);
}
} // namespace detail

/// @brief Writes @p scalar's fields into lane @p lane of @p packed; members that are not fields are untouched.
/// @tparam S A struct over f32 (`CoefficientsT<f32>`, `StateT<f32>`).
/// @tparam P The same struct template over Pack<f32, N>, with its fields in the same order.
template <class S, class P>
void set_lane(P& packed, std::size_t lane, const S& scalar) {
	detail::set_lane_at(packed, lane, scalar, std::make_index_sequence<std::tuple_size_v<decltype(S::fields)>>{});
}

/// @brief Lane @p lane of @p packed's fields, as the scalar struct @p S; members that are not fields are
/// value-initialised.
template <class S, class P>
[[nodiscard]] S get_lane(const P& packed, std::size_t lane) {
	S scalar{};
	detail::get_lane_at(packed, lane, scalar, std::make_index_sequence<std::tuple_size_v<decltype(S::fields)>>{});
	return scalar;
}

} // namespace deluge::dsp
