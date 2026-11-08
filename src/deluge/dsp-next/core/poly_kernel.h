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

#include <array>
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

/// @brief A float processor or generator kernel whose coefficients and state are written over V (f32 or Pack<f32, N>),
/// with `fields` naming each V member, and whose step is a template over V.
/// @details `Coefficients` is `CoefficientsT<f32>` and `State` is `StateT<f32>`, as Mono and Linked read them. A
/// coefficient member that is not a V (the mode) is not a field: every lane of one Poly or Bank shares it. A `prepare`
/// the kernel declares is a template over V, so it runs on a packed state as on a scalar one. It declares no
/// `kLoopState`: Poly, Bank and a chain on packed lanes step a lane's whole State, as Mono steps a kernel without one.
template <class K>
concept poly_kernel = (processor_kernel<K> || generator_kernel<K>) && std::same_as<typename K::value_type, f32>
                      && std::same_as<typename K::Coefficients, typename K::template CoefficientsT<f32>>
                      && std::same_as<typename K::State, typename K::template StateT<f32>>
                      && detail::declares_fields<typename K::template CoefficientsT<f32>>
                      && detail::declares_fields<typename K::template CoefficientsT<Pack<f32, 4>>>
                      && detail::declares_fields<typename K::template StateT<f32>>
                      && detail::declares_fields<typename K::template StateT<Pack<f32, 4>>>
                      && !detail::declares_context<K> && !detail::declares_loop_state<K>;

template <class S, class P>
void set_lane(P& packed, std::size_t lane, const S& scalar);
template <class S, class P>
[[nodiscard]] S get_lane(const P& packed, std::size_t lane, S scalar = S{});

namespace detail {
template <class S, class P, std::size_t... I>
void get_lane_at(const P& packed, std::size_t lane, S& scalar, std::index_sequence<I...> /*fields*/);
template <class S, class P>
requires declares_fields<S> && declares_fields<P>
void set_field(P& packed, std::size_t lane, const S& scalar);
template <class S, class P>
requires declares_fields<S> && declares_fields<P>
void get_field(const P& packed, std::size_t lane, S& scalar);

/// A V field: one lane.
template <class P>
requires requires(P p, std::size_t l, f32 x) { p.set(l, x); }
void set_field(P& packed, std::size_t lane, const f32& scalar) {
	packed.set(lane, scalar);
}
/// An array of fields: element by element.
template <class S, class P, std::size_t K>
void set_field(std::array<P, K>& packed, std::size_t lane, const std::array<S, K>& scalar) {
	for (std::size_t k = 0; k < K; ++k) {
		set_field(packed[k], lane, scalar[k]);
	}
}
/// A struct declaring its own fields: recurse.
template <class S, class P>
requires declares_fields<S> && declares_fields<P>
void set_field(P& packed, std::size_t lane, const S& scalar) {
	set_lane(packed, lane, scalar);
}

/// A V field: one lane.
template <class P>
requires requires(const P p, std::size_t l) {
	{ p.get(l) } -> std::same_as<f32>;
}
void get_field(const P& packed, std::size_t lane, f32& scalar) {
	scalar = packed.get(lane);
}
/// An array of fields: element by element.
template <class S, class P, std::size_t K>
void get_field(const std::array<P, K>& packed, std::size_t lane, std::array<S, K>& scalar) {
	for (std::size_t k = 0; k < K; ++k) {
		get_field(packed[k], lane, scalar[k]);
	}
}
/// A struct declaring its own fields: recurse, in place, so its members that are not fields keep their values.
template <class S, class P>
requires declares_fields<S> && declares_fields<P>
void get_field(const P& packed, std::size_t lane, S& scalar) {
	get_lane_at(packed, lane, scalar, std::make_index_sequence<std::tuple_size_v<decltype(S::fields)>>{});
}

template <class S, class P, std::size_t... I>
void set_lane_at(P& packed, std::size_t lane, const S& scalar, std::index_sequence<I...> /*fields*/) {
	static_assert(std::tuple_size_v<decltype(S::fields)> == std::tuple_size_v<decltype(P::fields)>,
	              "the scalar and packed structs name the same fields");
	(set_field(packed.*std::get<I>(P::fields), lane, scalar.*std::get<I>(S::fields)), ...);
}

template <class S, class P, std::size_t... I>
void get_lane_at(const P& packed, std::size_t lane, S& scalar, std::index_sequence<I...> /*fields*/) {
	static_assert(std::tuple_size_v<decltype(S::fields)> == std::tuple_size_v<decltype(P::fields)>,
	              "the scalar and packed structs name the same fields");
	(get_field(packed.*std::get<I>(P::fields), lane, scalar.*std::get<I>(S::fields)), ...);
}
} // namespace detail

/// @brief Writes @p scalar's fields into lane @p lane of @p packed; members that are not fields are untouched.
/// @tparam S A struct over f32 (`CoefficientsT<f32>`, `StateT<f32>`).
/// @tparam P The same struct template over Pack<f32, N>, with its fields in the same order.
template <class S, class P>
void set_lane(P& packed, std::size_t lane, const S& scalar) {
	detail::set_lane_at(packed, lane, scalar, std::make_index_sequence<std::tuple_size_v<decltype(S::fields)>>{});
}

/// @brief Lane @p lane of @p packed's fields, written over @p scalar: the fields, at every depth, are the lane's; the
/// members that are not fields (the modes) are @p scalar's, value-initialised by default.
template <class S, class P>
[[nodiscard]] S get_lane(const P& packed, std::size_t lane, S scalar) {
	detail::get_lane_at(packed, lane, scalar, std::make_index_sequence<std::tuple_size_v<decltype(S::fields)>>{});
	return scalar;
}

} // namespace deluge::dsp
