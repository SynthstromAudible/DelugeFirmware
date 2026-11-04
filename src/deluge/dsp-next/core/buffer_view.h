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
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

/// @file
/// @brief The seam between dsp-next's typed buffers and raw int32_t buffers.
///
/// dsp-next's kernels take samples as `std::span<Q<F>>` (Q31 for audio, Q24 for DX7's); the legacy synth, the app and
/// the effects still hold them as raw int32_t. typed_view() and raw_view() view one as the other, over the same
/// storage, where the two meet.
/// Each keeps its span's extent. Once those callers are typed, only the codec/DMA edge uses them. Each use is counted
/// by scripts/check_fixed_point_raw.py, whose `view` counts may only shrink.

namespace deluge::dsp {

namespace detail {
/// @brief Whether @p T is a 32-bit format, Q<F>: standard layout with its int32_t as its only member, so the two are
/// pointer-interconvertible and an array of one is laid out as an array of the other.
template <typename T>
concept q32 = std::same_as<T, Q<T::kFractionalBits>> && std::is_standard_layout_v<T>
              && sizeof(T) == sizeof(std::int32_t) && alignof(T) == alignof(std::int32_t);
} // namespace detail

/// @brief Raw samples viewed as @p T (Q31 unless named), over the same storage.
template <detail::q32 T = Q<31>, std::size_t E>
[[nodiscard, gnu::always_inline]] inline std::span<T, E> typed_view(std::span<std::int32_t, E> raw) noexcept {
	return std::span<T, E>{reinterpret_cast<T*>(raw.data()), raw.size()};
}
/// @brief Raw samples viewed as @p T (Q31 unless named), over the same storage, read-only.
template <detail::q32 T = Q<31>, std::size_t E>
[[nodiscard, gnu::always_inline]] inline std::span<const T, E>
typed_view(std::span<const std::int32_t, E> raw) noexcept {
	return std::span<const T, E>{reinterpret_cast<const T*>(raw.data()), raw.size()};
}
/// @brief Q<F> samples viewed as raw int32_t, over the same storage.
template <int F, std::size_t E>
[[nodiscard, gnu::always_inline]] inline std::span<std::int32_t, E> raw_view(std::span<Q<F>, E> typed) noexcept {
	return std::span<std::int32_t, E>{reinterpret_cast<std::int32_t*>(typed.data()), typed.size()};
}
/// @brief Q<F> samples viewed as raw int32_t, over the same storage, read-only.
template <int F, std::size_t E>
[[nodiscard, gnu::always_inline]] inline std::span<const std::int32_t, E>
raw_view(std::span<const Q<F>, E> typed) noexcept {
	return std::span<const std::int32_t, E>{reinterpret_cast<const std::int32_t*>(typed.data()), typed.size()};
}

} // namespace deluge::dsp
