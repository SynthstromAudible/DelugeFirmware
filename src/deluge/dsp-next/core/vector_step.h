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

#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/fixed_point_neon.h"
#include "dsp-next/core/sample.h"
#include <argon.hpp>
#include <argon/vectorize/for_each.hpp>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>

/// @file
/// @brief The vectors a block loop steps, and how one step loads and stores them: the vocabulary Mono and the
/// component mixins share.
namespace deluge::dsp {

namespace detail {
/// @brief The vector of @p T a block loop steps: defined for a float sample and for a Q<F> sample only.
template <typename T>
struct vector_of;

/// @brief A float sample's vector: Argon's.
template <std::floating_point T>
struct vector_of<T> {
	using type = Argon<T>;
};
/// @brief A Q<F> sample's vector: four of them in one QVec<F>.
template <int F>
struct vector_of<Q<F>> {
	using type = QVec<F>;
};
} // namespace detail

/// @brief The vector of @p T a block loop steps: `QVec<F>` for Q<F>, `Argon<f32>` for f32.
template <typename T>
using vector_t = detail::vector_of<T>::type;

/// @brief The samples of @p T in one vector_t<T>.
template <typename T>
inline constexpr std::ptrdiff_t lanes_v = static_cast<std::ptrdiff_t>(sizeof(vector_t<T>) / sizeof(T));

namespace detail {

/// @brief The arithmetic type `argon::vectorize::for_each` counts a block of @p T in: @p T itself when float, the
/// storage of a format type (a Q<31> block counts as int32_t).
template <typename T>
struct loop_lane {
	using type = T;
};
template <typename S, int F>
struct loop_lane<Fixed<S, F>> {
	using type = S;
};
template <typename T>
using loop_lane_t = loop_lane<T>::type;

/// @brief Whether the Argon loop step @p S is the partial last step of its block. Full steps are a distinct type.
template <typename S>
inline constexpr bool is_partial_step_v = false;

/// @brief A partial step of `argon::vectorize::for_each`.
template <typename T>
inline constexpr bool is_partial_step_v<argon::vectorize::Step<T, false>> = true;

/// @brief A partial step of `argon::vectorize::for_each_interleaved`.
template <typename T, std::size_t Stride>
inline constexpr bool is_partial_step_v<argon::vectorize::InterleavedStep<T, Stride, false>> = true;

/// @brief The vector V of the samples at @p p: Argon's load for a float T, FixedVec's for a format type.
template <typename V, typename T>
[[nodiscard, gnu::always_inline]] inline V load_vector(const T* p) {
	if constexpr (std::floating_point<T>) {
		return V::Load(p);
	}
	else {
		return V::load(p);
	}
}

/// @brief Stores the vector @p v to the samples at @p p.
template <typename V, typename T>
[[gnu::always_inline]] inline void store_vector(T* p, const V& v) {
	if constexpr (std::floating_point<T>) {
		v.StoreTo(p);
	}
	else {
		v.store(p);
	}
}

/// @brief This loop step's samples at @p base, as the vector V: lanes past the end of a partial step are zero and not
/// read. @p step counts the samples as loop_lane_t<T>; a float T loads through it, a format type through
/// FixedVec at the step's index and count.
template <typename V, typename Step, typename T>
[[nodiscard, gnu::always_inline]] inline V load_step(const Step& step, const T* base) {
	if constexpr (std::floating_point<T>) {
		return step.Load(base);
	}
	else if constexpr (is_partial_step_v<Step>) {
		return V::load(base + step.index(), step.count());
	}
	else {
		return V::load(base + step.index());
	}
}

/// @brief Stores @p v to this loop step's samples at @p base: lanes past the end of a partial step are not written.
template <typename V, typename Step, typename T>
[[gnu::always_inline]] inline void store_step(const Step& step, T* base, const V& v) {
	if constexpr (std::floating_point<T>) {
		step.Store(base, v);
	}
	else if constexpr (is_partial_step_v<Step>) {
		v.store(base + step.index(), step.count());
	}
	else {
		v.store(base + step.index());
	}
}

} // namespace detail

} // namespace deluge::dsp
