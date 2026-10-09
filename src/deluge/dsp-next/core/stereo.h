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
#include "dsp-next/core/lanes.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include <concepts>
#include <ranges>
#include <span>
#include <type_traits>

/// @file
/// @brief Stereo frames over the app's interleaved buffers, and Stereo<K>: a mono kernel run on both channels.
namespace deluge::dsp {

/// @brief An interleaved L/R q31 buffer viewed as frames: `size() / 2` frames; an odd trailing sample is outside.
/// @details Any contiguous range of q31 (a span, an array, a vector). A const range gives a
/// @ref fixed::StereoSignal, a mutable one a @ref fixed::StereoBuffer. The view aliases @p interleaved: a frame is
/// its two consecutive q31 samples, left first, which the layout assert in stereo_sample.h guarantees. The app's
/// buffers are int32 storage, so no object of another type is created. A range that owns its storage must be an
/// lvalue, since a view of a temporary would dangle.
template <std::ranges::contiguous_range R>
requires std::same_as<std::remove_cvref_t<std::ranges::range_reference_t<R>>, q31> && std::ranges::sized_range<R>
         && (std::is_lvalue_reference_v<R> || std::ranges::borrowed_range<R>)
[[nodiscard]] auto as_frames(R&& interleaved) {
	using Frame = std::conditional_t<std::is_const_v<std::remove_reference_t<std::ranges::range_reference_t<R>>>,
	                                 const StereoSample, StereoSample>;
	return std::span<Frame>{reinterpret_cast<Frame*>(std::ranges::data(interleaved)),
	                        std::ranges::size(interleaved) / 2};
}

/// @brief A mono q31 kernel run on both channels of a stereo frame: @ref Lanes over two lanes, left in lane 0, with
/// the channels named.
/// @tparam K The kernel: a scalar q31 kernel. Stereo<K> is a @ref processor; K must be a processor kernel. That
/// rejects a control kernel at the class too, with no clause of its own: a kernel has one form, so no control kernel
/// is a processor kernel. `Control` wraps a control kernel.
/// @details It behaves as @ref Lanes does: one Coefficients shared by both channels, one State per channel, `prepare`
/// left then right on every block with a Block per channel, and every frame stepped left before right and read whole
/// before it is written, so a kernel drawing from a stream both channels share (noise) draws for left before right,
/// and the input and output blocks may be the same buffer.
template <processor_kernel K>
requires std::same_as<typename K::value_type, q31> && std::same_as<typename K::step_type, q31>
class Stereo : public Lanes<K, 2, StereoSample> {
	using Base = Lanes<K, 2, StereoSample>; ///< The two-lane component Stereo names the channels of.

public:
	using Base::Base;

	/// @brief A snapshot of the left channel's state between blocks: lane 0's, by value, as @ref Lanes::state gives it.
	[[nodiscard]] [[gnu::always_inline]] typename K::State left() const { return this->state(0); }

	/// @brief A snapshot of the right channel's state between blocks: lane 1's, by value, as @ref Lanes::state gives
	/// it.
	[[nodiscard]] [[gnu::always_inline]] typename K::State right() const { return this->state(1); }
};

} // namespace deluge::dsp
