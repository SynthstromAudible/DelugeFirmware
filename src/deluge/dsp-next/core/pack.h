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

#include "dsp-next/core/frame.h"

#include <concepts>
#include <cstddef>
#include <type_traits>

/// @file
/// @brief Pack: N lanes as one value, whose operations apply their scalar counterparts lane by lane.
///
/// A kernel's step written once over its value type V runs on Q<31> for one stream and on `Pack<Q<31>, N>` for N
/// streams at once, each operation across every lane before the next. The same expression resolves for both: the
/// pack's operations are hidden friends, found by argument-dependent lookup on a pack and invisible otherwise, so they
/// never hide the scalar functions a kernel reaches by ordinary lookup (fixed_point.h's `add_wrap`, `mul_r`, ...;
/// fixed_point_lanes.h applies them to a pack). The scalar @ref select, @ref min and @ref max below complete the set
/// for a clamp.
///
/// Every operation is a fixed-trip loop over the lanes, in lane order, applying the scalar operation, so a pack's
/// result is bit for bit the scalar results side by side. Clang unrolls the loops, leaving one operation's lanes next
/// to each other: the independent instructions the core overlaps.
namespace deluge::dsp {

/// @brief N lanes of @p T as one value: each operation is its scalar counterpart applied lane by lane, in lane order.
/// @details A scalar operand broadcasts to every lane, as a kernel's coefficients, which every lane shares, do; so does
/// a T initialiser, so `V z{Q<31>{}}` is silence in every lane. `V z{}` is silence too, and is the form a StateT
/// member uses: the converting constructor takes exactly a T. A default-initialised pack's lanes are indeterminate,
/// as a T's would be. The comparisons give a pack of bool, which @ref select reads.
/// @tparam T The lane type: a `Fixed` for fixed_point_lanes.h's operations, any arithmetic type for the ones here.
/// @tparam N The number of lanes. At least 1.
template <class T, std::size_t N>
requires(N >= 1)
struct Pack {
	// A built-in array, not std::array: the layout is the N lanes alone, as a frame's is.
	// NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
	T lane[N]; ///< The lanes, lane 0 first.

	Pack() = default;

	/// @brief @p x in every lane: a scalar operand or initialiser broadcasts, as it does in a step written over V.
	template <std::same_as<T> U>
	// A scalar converts implicitly, so that one expression compiles for a scalar and for a pack.
	// NOLINTNEXTLINE(google-explicit-constructor,cppcoreguidelines-explicit-constructor,hicpp-explicit-conversions)
	[[gnu::always_inline]] constexpr Pack(U x) {
		for (std::size_t i = 0; i < N; ++i) {
			lane[i] = x;
		}
	}

	/// @brief @p x in every lane.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Pack broadcast(T x) { return Pack{x}; }

	/// @brief The lanes of @p f, lane for lane.
	[[nodiscard]] [[gnu::always_inline]] static constexpr Pack load(const Frame<T, N>& f) {
		Pack r;
		for (std::size_t i = 0; i < N; ++i) {
			r.lane[i] = f.lane[i];
		}
		return r;
	}

	/// @brief Writes the lanes to @p f, lane for lane.
	[[gnu::always_inline]] constexpr void store(Frame<T, N>& f) const {
		for (std::size_t i = 0; i < N; ++i) {
			f.lane[i] = lane[i];
		}
	}

	/// @brief @p a + @p b in each lane. As for a scalar, a sum that leaves T's range is undefined.
	/// @details Only for a T whose scalar sum is a T: a narrower T promotes to int, which a lane could not hold.
	[[nodiscard]] [[gnu::always_inline]] friend constexpr Pack operator+(Pack a, Pack b)
	requires(std::is_arithmetic_v<T> && std::same_as<decltype(T{} + T{}), T>)
	{
		Pack r;
		for (std::size_t i = 0; i < N; ++i) {
			r.lane[i] = a.lane[i] + b.lane[i];
		}
		return r;
	}

	/// @brief @p a − @p b in each lane. As for a scalar, a difference that leaves T's range is undefined.
	/// @details Only for a T whose scalar difference is a T: a narrower T promotes to int, which a lane could not hold.
	[[nodiscard]] [[gnu::always_inline]] friend constexpr Pack operator-(Pack a, Pack b)
	requires(std::is_arithmetic_v<T> && std::same_as<decltype(T{} - T{}), T>)
	{
		Pack r;
		for (std::size_t i = 0; i < N; ++i) {
			r.lane[i] = a.lane[i] - b.lane[i];
		}
		return r;
	}

	/// @brief @p a >> @p shift in each lane: arithmetic, rounding toward minus infinity. @p shift is 0 to 31.
	[[nodiscard]] [[gnu::always_inline]] friend constexpr Pack operator>>(Pack a, int shift)
	requires(std::is_integral_v<T> && !std::same_as<T, bool>)
	{
		Pack r;
		for (std::size_t i = 0; i < N; ++i) {
			r.lane[i] = static_cast<T>(a.lane[i] >> shift);
		}
		return r;
	}

	/// @brief @p a << @p shift in each lane, wrapping modulo 2^32 as a scalar shift does. @p shift is 0 to 31.
	[[nodiscard]] [[gnu::always_inline]] friend constexpr Pack operator<<(Pack a, int shift)
	requires(std::is_integral_v<T> && !std::same_as<T, bool>)
	{
		Pack r;
		for (std::size_t i = 0; i < N; ++i) {
			r.lane[i] = static_cast<T>(a.lane[i] << shift);
		}
		return r;
	}

	/// @brief Whether @p a < @p b, in each lane. A scalar operand broadcasts (the converting constructor), on
	/// either side: `pack < ceiling`.
	/// @param a,b The operands.
	/// @return Each lane's comparison, which @ref select reads.
	[[nodiscard]] [[gnu::always_inline]] friend constexpr Pack<bool, N> operator<(Pack a, Pack b)
	requires(!std::same_as<T, bool> && std::totally_ordered<T>)
	{
		Pack<bool, N> r;
		for (std::size_t i = 0; i < N; ++i) {
			r.lane[i] = a.lane[i] < b.lane[i];
		}
		return r;
	}

	/// @brief Whether @p a > @p b, in each lane. A scalar operand broadcasts (the converting constructor), on
	/// either side: `pack < ceiling`.
	/// @param a,b The operands.
	/// @return Each lane's comparison, which @ref select reads.
	[[nodiscard]] [[gnu::always_inline]] friend constexpr Pack<bool, N> operator>(Pack a, Pack b)
	requires(!std::same_as<T, bool> && std::totally_ordered<T>)
	{
		Pack<bool, N> r;
		for (std::size_t i = 0; i < N; ++i) {
			r.lane[i] = a.lane[i] > b.lane[i];
		}
		return r;
	}

	/// @brief Whether @p a <= @p b, in each lane. A scalar operand broadcasts (the converting constructor), on
	/// either side: `pack < ceiling`.
	/// @param a,b The operands.
	/// @return Each lane's comparison, which @ref select reads.
	[[nodiscard]] [[gnu::always_inline]] friend constexpr Pack<bool, N> operator<=(Pack a, Pack b)
	requires(!std::same_as<T, bool> && std::totally_ordered<T>)
	{
		Pack<bool, N> r;
		for (std::size_t i = 0; i < N; ++i) {
			r.lane[i] = a.lane[i] <= b.lane[i];
		}
		return r;
	}

	/// @brief Whether @p a >= @p b, in each lane. A scalar operand broadcasts (the converting constructor), on
	/// either side: `pack < ceiling`.
	/// @param a,b The operands.
	/// @return Each lane's comparison, which @ref select reads.
	[[nodiscard]] [[gnu::always_inline]] friend constexpr Pack<bool, N> operator>=(Pack a, Pack b)
	requires(!std::same_as<T, bool> && std::totally_ordered<T>)
	{
		Pack<bool, N> r;
		for (std::size_t i = 0; i < N; ++i) {
			r.lane[i] = a.lane[i] >= b.lane[i];
		}
		return r;
	}

	/// @brief Whether @p a == @p b, in each lane. A scalar operand broadcasts (the converting constructor), on
	/// either side: `pack < ceiling`.
	/// @param a,b The operands.
	/// @return Each lane's comparison, which @ref select reads.
	[[nodiscard]] [[gnu::always_inline]] friend constexpr Pack<bool, N> operator==(Pack a, Pack b)
	requires(!std::same_as<T, bool> && std::totally_ordered<T>)
	{
		Pack<bool, N> r;
		for (std::size_t i = 0; i < N; ++i) {
			r.lane[i] = a.lane[i] == b.lane[i];
		}
		return r;
	}

	/// @brief Whether @p a != @p b, in each lane. A scalar operand broadcasts (the converting constructor), on
	/// either side: `pack < ceiling`.
	/// @param a,b The operands.
	/// @return Each lane's comparison, which @ref select reads.
	[[nodiscard]] [[gnu::always_inline]] friend constexpr Pack<bool, N> operator!=(Pack a, Pack b)
	requires(!std::same_as<T, bool> && std::totally_ordered<T>)
	{
		Pack<bool, N> r;
		for (std::size_t i = 0; i < N; ++i) {
			r.lane[i] = a.lane[i] != b.lane[i];
		}
		return r;
	}

	/// @brief @p a where @p mask is set and @p b where it is clear, in each lane: the ternary `mask ? a : b`.
	[[nodiscard]] [[gnu::always_inline]] friend constexpr Pack select(Pack<bool, N> mask, Pack a, Pack b) {
		Pack r;
		for (std::size_t i = 0; i < N; ++i) {
			r.lane[i] = mask.lane[i] ? a.lane[i] : b.lane[i];
		}
		return r;
	}

	/// @brief The larger of @p a and @p b in each lane, as `a < b ? b : a`: a clamp to a floor.
	[[nodiscard]] [[gnu::always_inline]] friend constexpr Pack max(Pack a, Pack b)
	requires(std::is_arithmetic_v<T> && !std::same_as<T, bool>)
	{
		Pack r;
		for (std::size_t i = 0; i < N; ++i) {
			r.lane[i] = a.lane[i] < b.lane[i] ? b.lane[i] : a.lane[i];
		}
		return r;
	}

	/// @brief The smaller of @p a and @p b in each lane, as `a > b ? b : a`: a clamp to a ceiling.
	[[nodiscard]] [[gnu::always_inline]] friend constexpr Pack min(Pack a, Pack b)
	requires(std::is_arithmetic_v<T> && !std::same_as<T, bool>)
	{
		Pack r;
		for (std::size_t i = 0; i < N; ++i) {
			r.lane[i] = a.lane[i] > b.lane[i] ? b.lane[i] : a.lane[i];
		}
		return r;
	}
};

/// @brief @p a if @p mask, else @p b: the scalar form of a pack's select, so that a clamp written over V compiles for
/// a scalar too. A ternary, so that it needs no inlining.
/// @param mask The condition.
/// @param a    The result when @p mask is true.
/// @param b    The result when @p mask is false.
/// @return @p a or @p b.
template <class T>
requires std::is_arithmetic_v<T>
[[nodiscard]] [[gnu::always_inline]] constexpr T select(bool mask, T a, T b) {
	return mask ? a : b;
}

/// @brief The larger of @p a and @p b, as `a < b ? b : a`: the scalar form of a pack's max.
/// @param a The first value.
/// @param b The second value.
/// @return The larger value.
template <class T>
requires std::is_arithmetic_v<T>
[[nodiscard]] [[gnu::always_inline]] constexpr T max(T a, T b) {
	return a < b ? b : a;
}

/// @brief The smaller of @p a and @p b, as `a > b ? b : a`: the scalar form of a pack's min.
/// @param a The first value.
/// @param b The second value.
/// @return The smaller value.
template <class T>
requires std::is_arithmetic_v<T>
[[nodiscard]] [[gnu::always_inline]] constexpr T min(T a, T b) {
	return a > b ? b : a;
}

} // namespace deluge::dsp
