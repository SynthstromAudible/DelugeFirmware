/**
 * Copyright (c) 2025 Katherine Whitlock
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
#include "deluge/util/fixedpoint.h"
#include <span>
#include <string_view>

namespace deluge::dsp {

/// @brief A simple type alias for a sample.
/// @tparam T The type of the sample.
/// @details This type alias is used to represent a single sample of audio data.
template <typename T>
using Sample = T;

/// @brief A writeable contiguous region of memory.
/// @tparam T The type in the buffer.
/// @details This type alias is used to represent a buffer of audio data that can be written to.
template <typename T>
using Buffer = std::span<T>;

/// @brief A read-only contiguous region of memory.
/// @tparam T The type in the signal.
/// @details This type alias is used to represent a signal of audio data that can be read from.
template <typename T>
using Signal = std::span<const T>;

/// @brief A stereo sample.
/// @tparam T The type of the sample.
/// @details This struct is used to represent a stereo sample, which consists of two channels (left and right).
template <typename T>
struct StereoSample {
	Sample<T> l;
	Sample<T> r;

	[[gnu::always_inline]] static constexpr StereoSample fromMono(Sample<T> sample) { return {sample, sample}; }

	bool operator==(const StereoSample& other) const { return l == other.l && r == other.r; }
	bool operator!=(const StereoSample& other) const { return !(*this == other); }
	StereoSample operator+(const StereoSample& other) const { return {l + other.l, r + other.r}; }
	StereoSample operator-(const StereoSample& other) const { return {l - other.l, r - other.r}; }
	StereoSample operator*(T scalar) const { return {l * scalar, r * scalar}; }
	StereoSample& operator+=(const StereoSample& other) {
		l += other.l;
		r += other.r;
		return *this;
	}
};

template <typename T>
using StereoBuffer = Buffer<StereoSample<T>>;

template <typename T>
using StereoSignal = Signal<StereoSample<T>>;

namespace fixed_point {
using Sample = Sample<FixedPoint<31>>;
using Buffer = Buffer<FixedPoint<31>>;
using Signal = Signal<FixedPoint<31>>;
using StereoSample = StereoSample<FixedPoint<31>>;
using StereoBuffer = StereoBuffer<FixedPoint<31>>;
using StereoSignal = StereoSignal<FixedPoint<31>>;
} // namespace fixed_point

namespace floating_point {
using Sample = Sample<float>;
using Buffer = Buffer<float>;
using Signal = Signal<float>;
using StereoSample = StereoSample<float>;
using StereoBuffer = StereoBuffer<float>;
using StereoSignal = StereoSignal<float>;
} // namespace floating_point

} // namespace deluge::dsp
