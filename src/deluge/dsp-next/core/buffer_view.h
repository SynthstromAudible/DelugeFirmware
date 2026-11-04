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
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

/// @file
/// @brief The seam between dsp-next's typed buffers and raw int32_t buffers.
///
/// dsp-next's kernels take Q31 samples as `std::span<Q<31>>`; the legacy synth, the app and the effects still hold
/// them as raw int32_t. typed_view() and raw_view() view one as the other, over the same storage, where the two meet.
/// Each keeps its span's extent. Once those callers are typed, only the codec/DMA edge uses them. Each use is counted
/// by scripts/check_fixed_point_raw.py, whose `view` counts may only shrink.

namespace deluge::dsp {

namespace detail {
// A Q<31> is standard layout with its int32_t as its only member, so the two are pointer-interconvertible and an array
// of one is laid out as an array of the other.
static_assert(std::is_standard_layout_v<Q<31>> && sizeof(Q<31>) == sizeof(std::int32_t)
              && alignof(Q<31>) == alignof(std::int32_t));
} // namespace detail

/// @brief Raw Q31 samples viewed as Q<31>, over the same storage.
template <std::size_t E>
[[nodiscard, gnu::always_inline]] inline std::span<Q<31>, E> typed_view(std::span<std::int32_t, E> raw) noexcept {
	return std::span<Q<31>, E>{reinterpret_cast<Q<31>*>(raw.data()), raw.size()};
}
/// @brief Raw Q31 samples viewed as Q<31>, over the same storage, read-only.
template <std::size_t E>
[[nodiscard, gnu::always_inline]] inline std::span<const Q<31>, E>
typed_view(std::span<const std::int32_t, E> raw) noexcept {
	return std::span<const Q<31>, E>{reinterpret_cast<const Q<31>*>(raw.data()), raw.size()};
}
/// @brief Q<31> samples viewed as raw int32_t, over the same storage.
template <std::size_t E>
[[nodiscard, gnu::always_inline]] inline std::span<std::int32_t, E> raw_view(std::span<Q<31>, E> typed) noexcept {
	return std::span<std::int32_t, E>{reinterpret_cast<std::int32_t*>(typed.data()), typed.size()};
}
/// @brief Q<31> samples viewed as raw int32_t, over the same storage, read-only.
template <std::size_t E>
[[nodiscard, gnu::always_inline]] inline std::span<const std::int32_t, E>
raw_view(std::span<const Q<31>, E> typed) noexcept {
	return std::span<const std::int32_t, E>{reinterpret_cast<const std::int32_t*>(typed.data()), typed.size()};
}

} // namespace deluge::dsp
