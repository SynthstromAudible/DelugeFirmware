/*
 * Copyright © 2020-2026 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"           // kInterpolationMaxNumSamples
#include "dsp-next/core/stereo_sample.h" // deluge::dsp::StereoSample
#include <argon.hpp>
#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::dsp::fixed {

namespace detail {
/// Shift a 16-tap window held as two 8-lane halves toward older indices by @p N (1 ... 15): the value at i ends up at
/// i + N and the oldest fall off. `vext` takes its offset as an immediate, hence the template. The @p N vacated head
/// slots come out holding unspecified values, which the caller overwrites.
template <int N>
[[gnu::always_inline]] inline void shift_window_by(Argon<int16_t>& low, Argon<int16_t>& high) {
	static_assert(N > 0 && N < 16);
	if constexpr (N < 8) {
		// The top half takes the upper end of the bottom half, the bottom half moves up within itself
		high = low.Extract<8 - N>(high);
		low = low.Extract<8 - N>(low);
	}
	else if constexpr (N == 8) {
		high = low;
	}
	else {
		// Only the top half keeps anything, taken from the bottom half
		high = low.Extract<16 - N>(low);
	}
}
} // namespace detail

/// @brief A stereo windowed-sinc / linear sample interpolator over a sliding tap window (q31 backend).
///
/// Holds the most recent @ref kInterpolationMaxNumSamples input samples per channel, the *tap window*: newest at
/// index 0, oldest at the end. It reads a sub-sample-accurate output frame with @ref interpolate (windowed sinc, with
/// the @p which_kernel-selected oversampling variant of @ref kWindowedSincKernel) or, on the cheap path,
/// @ref interpolate_linear (the two newest taps).
///
/// The owner feeds the window through feeds, which write the slots an operation opened in the order the samples
/// arrive:
/// - @ref advance shifts the window by n samples and returns a @ref HeadFeed for the n vacated head slots, oldest
///   first; @ref fill_head returns one without shifting; @ref advance_linear is the two-tap form.
/// - @ref fill_tail returns a @ref TailFeed that writes from a slot toward the oldest, newest first: a window filled
///   back from a play position.
/// - @ref push_l / @ref push_r advance one channel by one sample.
/// - @ref clear zeroes a range; @ref widen and @ref narrow move the window when the interpolation width changes.
/// A mono owner feeds the left window only.
class Interpolator {
public:
	using Window = std::array<int16_t, kInterpolationMaxNumSamples>;

	/// @brief Writes a run of head slots, from the oldest of them to slot 0: the samples that follow the window's
	/// newest, in the order they arrive.
	class HeadFeed {
	public:
		/// Writes the next slot's left sample (a mono owner).
		[[gnu::always_inline]] void put(int16_t l) { owner_->buffer_l_[--remaining_] = l; }
		/// Writes the next slot's left and right samples.
		[[gnu::always_inline]] void put(int16_t l, int16_t r) {
			--remaining_;
			owner_->buffer_l_[remaining_] = l;
			owner_->buffer_r_[remaining_] = r;
		}
		/// Zeroes the next slot on both channels.
		[[gnu::always_inline]] void silence() { put(0, 0); }
		/// The slots still to write; 0 once slot 0 is written.
		[[nodiscard]] [[gnu::always_inline]] std::size_t remaining() const { return remaining_; }

	private:
		friend class Interpolator;
		[[gnu::always_inline]] HeadFeed(Interpolator& owner, std::size_t slots) : owner_(&owner), remaining_(slots) {}
		Interpolator* owner_; ///< The interpolator whose windows it writes; never null.
		std::size_t remaining_;
	};

	/// @brief Writes slots from one toward the oldest: the samples before the slot, newest first.
	class TailFeed {
	public:
		/// Writes the next slot's left sample (a mono owner).
		[[gnu::always_inline]] void put(int16_t l) { owner_->buffer_l_[index_++] = l; }
		/// Writes the next slot's left and right samples.
		[[gnu::always_inline]] void put(int16_t l, int16_t r) {
			owner_->buffer_l_[index_] = l;
			owner_->buffer_r_[index_] = r;
			++index_;
		}
		/// Zeroes the next slot on both channels.
		[[gnu::always_inline]] void silence() { put(0, 0); }
		/// The slot the next write lands in.
		[[nodiscard]] [[gnu::always_inline]] std::size_t index() const { return index_; }

	private:
		friend class Interpolator;
		[[gnu::always_inline]] TailFeed(Interpolator& owner, std::size_t from) : owner_(&owner), index_(from) {}
		Interpolator* owner_; ///< The interpolator whose windows it writes; never null.
		std::size_t index_;
	};

	/// @brief Sub-sample-accurate windowed-sinc interpolation of the current window.
	/// @param channels 1 = mono (left window only), 2 = stereo (both).
	/// @param which_kernel the oversampling-kernel variant (from `WindowedSincKernel::variant_for`, by playback rate).
	/// @param osc_pos the sub-sample phase within the window, Q24.
	/// @return the interpolated frame (right channel left untouched when @p channels == 1).
	[[nodiscard]] StereoSample interpolate(std::size_t channels, int32_t which_kernel, uint32_t osc_pos) const;

	/// @brief 2-tap linear interpolation between the two newest window samples — the cheap alternative.
	/// @details An integer lerp: the Q15 samples times Q15 weights that sum to 32767, so the frame is Q30 of the
	/// samples, as @ref interpolate's is.
	/// @param channels 1 = mono, 2 = stereo. @param phase the sub-sample phase, Q24.
	[[nodiscard]] StereoSample interpolate_linear(std::size_t channels, uint32_t phase) const;

	/// @brief Shifts both windows by @p n samples toward the oldest.
	/// @return A feed for the @p n vacated head slots, which hold unspecified values until it writes them.
	[[nodiscard]] [[gnu::always_inline]] constexpr HeadFeed advance(std::size_t n) {
		shift_window(buffer_l_, n);
		shift_window(buffer_r_, n);
		return {*this, n};
	}

	/// @brief A feed for head slots @p n − 1 … 0, with no shift: a window filled forward from a play position.
	[[nodiscard]] [[gnu::always_inline]] HeadFeed fill_head(std::size_t n) { return {*this, n}; }

	/// @brief The two-tap form of @ref advance, which only the newest two taps are read by: a step of one moves tap 0
	/// to tap 1 on @p channels channels and feeds tap 0; a longer step feeds both. Taps 2 on are left as they are.
	[[nodiscard]] [[gnu::always_inline]] HeadFeed advance_linear(std::size_t n, std::size_t channels) {
		if (n >= 2) {
			return {*this, 2};
		}
		buffer_l_[1] = buffer_l_[0];
		if (channels == 2) {
			buffer_r_[1] = buffer_r_[0];
		}
		return {*this, 1};
	}

	/// @brief A feed from slot @p from toward the oldest.
	[[nodiscard]] [[gnu::always_inline]] TailFeed fill_tail(std::size_t from) { return {*this, from}; }

	/// Advance the left window by one sample, inserting @p value at the head.
	[[gnu::always_inline]] constexpr void push_l(int16_t value) {
		shift_window(buffer_l_, 1);
		buffer_l_[0] = value;
	}
	/// Advance the right window by one sample, inserting @p value at the head.
	[[gnu::always_inline]] constexpr void push_r(int16_t value) {
		shift_window(buffer_r_, 1);
		buffer_r_[0] = value;
	}

	/// @brief Zeroes slots [@p from, @p to) of the left window, and of the right when @p channels is 2.
	[[gnu::always_inline]] void clear(std::size_t from, std::size_t to, std::size_t channels) {
		for (std::size_t i = from; i < to; ++i) {
			buffer_l_[i] = 0;
			if (channels == 2) {
				buffer_r_[i] = 0;
			}
		}
	}

	/// @brief The interpolation width shrank: slot i takes slot i + @p offset, for i in [0, @p count).
	[[gnu::always_inline]] void narrow(std::size_t offset, std::size_t count, std::size_t channels) {
		for (std::size_t i = 0; i < count; ++i) {
			buffer_l_[i] = buffer_l_[i + offset];
			if (channels == 2) {
				buffer_r_[i] = buffer_r_[i + offset];
			}
		}
	}

	/// @brief The interpolation width grew: slot i + @p offset takes slot i, for i in [0, @p count), in rising i.
	/// @details Rising i reads slots it has already written when @p offset < @p count, so the newest taps are copied
	/// along rather than the window moved (quirk Q11,
	/// docs/superpowers/specs/2026-11-05-pitch-shift-dsp-next-design.md).
	[[gnu::always_inline]] void widen(std::size_t offset, std::size_t count, std::size_t channels) {
		for (std::size_t i = 0; i < count; ++i) {
			buffer_l_[i + offset] = buffer_l_[i];
			if (channels == 2) {
				buffer_r_[i + offset] = buffer_r_[i];
			}
		}
	}

	/// The left window, newest first.
	[[nodiscard]] const Window& window_l() const { return buffer_l_; }
	/// The right window, newest first.
	[[nodiscard]] const Window& window_r() const { return buffer_r_; }

private:
	/// Shift @p window toward older indices by @p n samples, vacating the @p n head slots (the discarded tail falls
	/// off the end; the vacated slots hold unspecified values until the caller fills them). Shared by @ref push_l /
	/// @ref push_r / @ref advance. Two NEON `vext`s per window instead of up to 15 halfword copies, which neither
	/// GCC nor Clang vectorises for n < 8 (the loop reads slots it has just written).
	[[gnu::always_inline]] static constexpr void shift_window(Window& window, std::size_t n) {
		if consteval {
			for (int32_t i = kInterpolationMaxNumSamples - 1; i >= static_cast<int32_t>(n); i--) {
				window[i] = window[i - static_cast<int32_t>(n)];
			}
		}
		else {
			static_assert(kInterpolationMaxNumSamples == 2 * Argon<int16_t>::lanes);
			if (n == 0 || n >= kInterpolationMaxNumSamples) {
				return; // 0: nothing moves. 16 and more: nothing stays, the caller refills every slot
			}
			auto low = Argon<int16_t>::Load(window.data());
			auto high = Argon<int16_t>::Load(window.data() + Argon<int16_t>::lanes);
			switch (n) {
			case 1:
				detail::shift_window_by<1>(low, high);
				break;
			case 2:
				detail::shift_window_by<2>(low, high);
				break;
			case 3:
				detail::shift_window_by<3>(low, high);
				break;
			case 4:
				detail::shift_window_by<4>(low, high);
				break;
			case 5:
				detail::shift_window_by<5>(low, high);
				break;
			case 6:
				detail::shift_window_by<6>(low, high);
				break;
			case 7:
				detail::shift_window_by<7>(low, high);
				break;
			case 8:
				detail::shift_window_by<8>(low, high);
				break;
			case 9:
				detail::shift_window_by<9>(low, high);
				break;
			case 10:
				detail::shift_window_by<10>(low, high);
				break;
			case 11:
				detail::shift_window_by<11>(low, high);
				break;
			case 12:
				detail::shift_window_by<12>(low, high);
				break;
			case 13:
				detail::shift_window_by<13>(low, high);
				break;
			case 14:
				detail::shift_window_by<14>(low, high);
				break;
			case 15:
				detail::shift_window_by<15>(low, high);
				break;
			}
			low.StoreTo(window.data());
			high.StoreTo(window.data() + Argon<int16_t>::lanes);
		}
	}

	/// The per-channel tap windows (newest sample at index 0). Quadword-aligned for the NEON dot product, and
	/// value-initialised so an unfed tap reads as a defined 0 rather than allocation residue.
	alignas(sizeof(int32_t) * 4) Window buffer_l_{};
	alignas(sizeof(int32_t) * 4) Window buffer_r_{};
};

} // namespace deluge::dsp::fixed
