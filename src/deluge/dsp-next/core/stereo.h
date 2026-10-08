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

#include "dsp-next/core/concepts.h"
#include "dsp-next/core/processor.h"
#include "dsp-next/core/types.h"
#include <concepts>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

/// @file
/// @brief Stereo frames over the app's interleaved buffers, and a mono processor run on both channels.
namespace deluge::dsp {

/// @brief An interleaved L/R q31 buffer viewed as frames: `size() / 2` frames; an odd trailing sample is outside.
/// @details Any contiguous range of q31 (a span, an array, a vector). A const range gives a
/// @ref fixed::StereoSignal, a mutable one a @ref fixed::StereoBuffer. The view aliases @p interleaved: a frame is
/// its two consecutive q31 samples, left first, which the layout assert in stereo_sample.h guarantees. The app's
/// buffers are int32 storage, so no object of another type is created.
template <std::ranges::contiguous_range R>
requires std::same_as<std::remove_cvref_t<std::ranges::range_reference_t<R>>, q31> && std::ranges::sized_range<R>
[[nodiscard]] auto as_frames(R&& interleaved) {
	using Frame = std::conditional_t<std::is_const_v<std::remove_reference_t<std::ranges::range_reference_t<R>>>,
	                                 const StereoSample, StereoSample>;
	return std::span<Frame>{reinterpret_cast<Frame*>(std::ranges::data(interleaved)),
	                        std::ranges::size(interleaved) / 2};
}

/// @brief A mono q31 processor run on each channel of a stereo frame, left fully before right.
/// @details The order matters when both channels draw from one shared noise stream: left's draws come first on
/// every frame.
/// @tparam P the mono processor; one instance per channel.
template <sample_processor P>
requires std::same_as<typename P::value_type, q31>
class Stereo : public Processor<Stereo<P>, StereoSample> {
public:
	Stereo() = default;

	/// @brief A stereo processor from one mono processor per channel.
	Stereo(P left, P right) : left_{std::move(left)}, right_{std::move(right)} {}

	/// @brief Process one frame: the left channel, then the right.
	[[nodiscard, gnu::always_inline]] StereoSample render(StereoSample frame) {
		const q31 l = left_.render(frame.l);
		const q31 r = right_.render(frame.r);
		return {.l = l, .r = r};
	}

	/// @brief Configure both channels from one @p settings; present when P has `configure(const P::Settings&)`.
	template <std::same_as<P> Q = P>
	void configure(const typename Q::Settings& settings)
	requires requires(Q q, const typename Q::Settings& s) { q.configure(s); }
	{
		left_.configure(settings);
		right_.configure(settings);
	}

	/// @brief The left channel's processor.
	[[nodiscard]] P& left() { return left_; }
	/// @brief The right channel's processor.
	[[nodiscard]] P& right() { return right_; }

private:
	P left_{};
	P right_{};
};

} // namespace deluge::dsp
