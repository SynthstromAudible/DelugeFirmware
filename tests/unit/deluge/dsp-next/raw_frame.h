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

// The references' stereo frame: the legacy app's raw pair of Q31 words. The references stay raw; a spec that shares a
// block between a reference and a typed kernel views it through raw_frames() or typed_frames().

#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/wrapping.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace deluge::dsp::test {

/// @brief A stereo frame of raw Q31 words, laid out as StereoSample is.
struct RawFrame {
	int32_t l; ///< The left channel, Q31.
	int32_t r; ///< The right channel, Q31.

	/// A frame with both channels set to @p mono.
	[[nodiscard]] static constexpr RawFrame from_mono(int32_t mono) { return {.l = mono, .r = mono}; }
	/// @brief @p a + @p b in each channel, wrapping modulo 2^32.
	[[nodiscard]] friend constexpr RawFrame operator+(RawFrame a, RawFrame b) {
		return {.l = wrapping_add(a.l, b.l), .r = wrapping_add(a.r, b.r)};
	}
	/// @brief Adds @p rhs in each channel, wrapping modulo 2^32.
	constexpr RawFrame& operator+=(RawFrame rhs) {
		l = wrapping_add(l, rhs.l);
		r = wrapping_add(r, rhs.r);
		return *this;
	}
	friend constexpr bool operator==(RawFrame a, RawFrame b) = default;
};

static_assert(std::is_standard_layout_v<RawFrame> && sizeof(RawFrame) == sizeof(StereoSample)
                  && offsetof(RawFrame, l) == offsetof(StereoSample, l)
                  && offsetof(RawFrame, r) == offsetof(StereoSample, r),
              "a RawFrame block is a StereoSample block's words");

/// @brief @p frame's words.
[[nodiscard]] constexpr RawFrame raw_frame(StereoSample frame) {
	return {.l = frame.l.raw(), .r = frame.r.raw()};
}
/// @brief @p frame's words as a typed frame.
[[nodiscard]] constexpr StereoSample typed_frame(RawFrame frame) {
	return {.l = Q<31>::from_raw(frame.l), .r = Q<31>::from_raw(frame.r)};
}

/// @brief Typed frames viewed as raw ones, over the same storage.
template <std::size_t E>
[[nodiscard]] inline std::span<RawFrame, E> raw_frames(std::span<StereoSample, E> typed) {
	return std::span<RawFrame, E>{reinterpret_cast<RawFrame*>(typed.data()), typed.size()};
}
/// @brief Typed frames viewed as raw ones, over the same storage, read-only.
template <std::size_t E>
[[nodiscard]] inline std::span<const RawFrame, E> raw_frames(std::span<const StereoSample, E> typed) {
	return std::span<const RawFrame, E>{reinterpret_cast<const RawFrame*>(typed.data()), typed.size()};
}
/// @brief Raw frames viewed as typed ones, over the same storage.
template <std::size_t E>
[[nodiscard]] inline std::span<StereoSample, E> typed_frames(std::span<RawFrame, E> raw) {
	return std::span<StereoSample, E>{reinterpret_cast<StereoSample*>(raw.data()), raw.size()};
}
/// @brief Raw frames viewed as typed ones, over the same storage, read-only.
template <std::size_t E>
[[nodiscard]] inline std::span<const StereoSample, E> typed_frames(std::span<const RawFrame, E> raw) {
	return std::span<const StereoSample, E>{reinterpret_cast<const StereoSample*>(raw.data()), raw.size()};
}

} // namespace deluge::dsp::test
