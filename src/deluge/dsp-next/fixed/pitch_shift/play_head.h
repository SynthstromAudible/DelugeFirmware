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

#include "definitions_cxx.hpp" // kInterpolationMaxNumSamples
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mode.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/interpolation/interpolator.h"
#include "dsp-next/fixed/pitch_shift/input_history.h"

#include <cstdint>
#include <type_traits>

/// @file
/// @brief One of the live pitch shifter's play heads: the input history read back at the shifted rate, or as it was.

namespace deluge::dsp::fixed::pitch_shift {

/// @brief No percussiveness entry noted since the head's hop: the next onset check reads afresh (quirk Q9,
/// docs/dev/live_pitch_shifter_quirks.md).
inline constexpr uint32_t kNoPercussivenessEntry = 0xFFFFFFFF;

/// @brief What a play head reads.
enum class HeadKind : uint8_t {
	DIRECT,     ///< The history's frames as they are, one per output sample: no shift.
	REPITCHING, ///< The history interpolated at the shifted rate.
};

/// @brief A play head's memory, the same for a mono and a stereo history.
struct PlayHeadState {
	HeadKind kind = HeadKind::DIRECT;
	RingPosition cursor;               ///< The next history frame the head reads.
	uint32_t phase = 0;                ///< The sub-sample phase between taps, Q24.
	Interpolator interpolator{};       ///< The window of recent taps, repitching.
	uint32_t percussiveness_entry = 0; ///< The percussiveness entry the shifter last checked at this head.
	Q<31> amplitude{};                 ///< The segment's amplitude, before its next step.
	Q<31> amplitude_step{};            ///< The segment's amplitude step per sample.
};

/// @brief One play head over a live input's history, as a processor kernel adding the head's output into a block.
/// @details Each step first steps the segment's amplitude, then reads the head and adds it, times the amplitude, to the
/// sample it is given. Repitching, the head advances its phase by the shift and feeds the interpolator each history
/// frame it passes (after a jump of more than 16 frames, only the last 16; quirk Q7), then interpolates, windowed sinc
/// or linear; the product is scaled by 16. Direct, it reads one history frame per step, scaled by 8.
///
/// The owner configures each block with the history (which the host's pool may move between blocks), the shift, the
/// sinc kernel's variant for it, and the head's kind and interpolation; it sends @ref start_segment before each
/// segment it renders and @ref begin_hop when the head is placed for a new hop.
/// @tparam Frame Q<31> for a mono history, StereoSample for a stereo one.
template <typename Frame>
struct PlayHead {
	using value_type = Frame; ///< The block the head adds into.
	using step_type = Frame;  ///< One frame per step.

	/// @brief How the block reads the head.
	enum class Mode : uint8_t {
		DIRECT,            ///< The frames as they are.
		REPITCHING_SINC,   ///< Interpolated through the windowed sinc.
		REPITCHING_LINEAR, ///< Interpolated linearly between the two newest taps.
	};
	using Modes = ModeList<Mode::DIRECT, Mode::REPITCHING_SINC, Mode::REPITCHING_LINEAR>;

	/// The level each read is added at: the rounded product moved to Q31, then this power of two.
	static constexpr int kDirectLevel = 3;    // ×8
	static constexpr int kRepitchedLevel = 4; // ×16
	/// The channels the history holds.
	static constexpr std::size_t kChannels = std::is_same_v<Frame, StereoSample> ? 2 : 1;

	struct Settings {
		const InputHistory<Frame>* history = nullptr;
		uint32_t phase_increment = 1U << 24; ///< The shift, 8.24.
		int32_t kernel_variant = 0;          ///< The windowed sinc's variant for the shift.
		HeadKind kind = HeadKind::DIRECT;    ///< The head's kind, from its state.
		bool linear = false;                 ///< Linear interpolation rather than the windowed sinc.
	};

	struct Coefficients {
		const InputHistory<Frame>* history = nullptr;
		uint32_t phase_increment = 1U << 24;
		int32_t kernel_variant = 0;
		Mode mode = Mode::DIRECT;
	};

	using State = PlayHeadState;
	/// The interpolator's windows live in the head: the loop steps them in place.
	static constexpr bool kStateInPlace = true;

	/// @brief Where a hop places the head: its cursor and kind, and its phase when repitching.
	struct Hop {
		RingPosition cursor;
		uint32_t phase = 0; ///< Q24; a direct head keeps the phase it had.
		HeadKind kind = HeadKind::DIRECT;
	};

	[[nodiscard]] [[gnu::always_inline]] static constexpr Coefficients coefficients(const Settings& s) {
		Mode mode = Mode::DIRECT;
		if (s.kind == HeadKind::REPITCHING) {
			mode = s.linear ? Mode::REPITCHING_LINEAR : Mode::REPITCHING_SINC;
		}
		return {.history = s.history,
		        .phase_increment = s.phase_increment,
		        .kernel_variant = s.kernel_variant,
		        .mode = mode};
	}

	/// @brief Event: the next segment's amplitude, which each step steps before it reads.
	static void start_segment(State& s, Q<31> amplitude, Q<31> step) {
		s.amplitude = amplitude;
		s.amplitude_step = step;
	}

	/// @brief Event: the head placed for a new hop. Repitching, its window is refilled from the history behind the
	/// cursor, newest first, a frame not yet written reading as silence. The onset check starts afresh.
	static void begin_hop(const Coefficients& c, State& s, const Hop& hop) {
		s.cursor = hop.cursor;
		s.kind = hop.kind;
		if (hop.kind == HeadKind::REPITCHING) {
			auto feed = s.interpolator.fill_tail(0);
			for (int32_t back = 1; back <= kInterpolationMaxNumSamples; ++back) {
				const RingPosition p = hop.cursor - back;
				put_tap(feed, *c.history, p, c.history->written(p));
			}
			s.phase = hop.phase;
		}
		s.percussiveness_entry = kNoPercussivenessEntry;
	}

	/// @brief Event: the percussiveness entry the shifter checked at this head.
	static void note_percussiveness_entry(State& s, uint32_t entry) { s.percussiveness_entry = entry; }

	/// @brief Event: the head takes @p other's place: what was the newer head becomes the older.
	static void take(State& s, const State& other) { s = other; }

	/// @brief One frame at mode @p M: @p x plus the head times the amplitude.
	template <Mode M>
	[[nodiscard]] [[gnu::always_inline]] static Frame step(const Coefficients& c, const EmptyBlock& /*b*/, State& s,
	                                                       Frame x) {
		const InputHistory<Frame>& history = *c.history;
		if constexpr (M == Mode::DIRECT) {
			s.amplitude = add_wrap(s.amplitude, s.amplitude_step);
			const Frame read = history.at(s.cursor);
			s.cursor = s.cursor + 1;
			return add_level<kDirectLevel>(x, read, s.amplitude);
		}
		else {
			s.phase += c.phase_increment;
			uint32_t jump = s.phase >> 24;
			if (jump != 0) {
				s.phase &= kPhaseMask;
				if (jump > kInterpolationMaxNumSamples) {
					// Only the last 16 frames of a long jump reach the window (quirk Q7)
					s.cursor = s.cursor + static_cast<int32_t>(jump - kInterpolationMaxNumSamples);
					jump = kInterpolationMaxNumSamples;
				}
				auto feed = s.interpolator.advance(jump);
				while (feed.remaining() != 0) {
					put_tap(feed, history, s.cursor, true);
					s.cursor = s.cursor + 1;
				}
			}
			s.amplitude = add_wrap(s.amplitude, s.amplitude_step);
			const StereoSample read = M == Mode::REPITCHING_SINC
			                              ? s.interpolator.interpolate(kChannels, c.kernel_variant, s.phase)
			                              : s.interpolator.interpolate_linear(kChannels, s.phase);
			if constexpr (std::is_same_v<Frame, StereoSample>) {
				return add_level<kRepitchedLevel>(x, read, s.amplitude);
			}
			else {
				return add_level<kRepitchedLevel>(x, read.l, s.amplitude);
			}
		}
	}

private:
	/// The phase's fraction: its low 24 bits.
	static constexpr uint32_t kPhaseMask = (1U << 24) - 1;

	/// @brief @p x plus @p read times @p amplitude, the rounded product moved to Q31 and scaled by 2^Level, wrapping.
	template <int Level>
	[[nodiscard]] [[gnu::always_inline]] static Q<31> add_level(Q<31> x, Q<31> read, Q<31> amplitude) {
		return add_wrap(x, scale_shl_wrap<Level>(mul_r_as_wrap<Q<31>>(read, amplitude)));
	}
	template <int Level>
	[[nodiscard]] [[gnu::always_inline]] static StereoSample add_level(StereoSample x, StereoSample read,
	                                                                   Q<31> amplitude) {
		return {.l = add_level<Level>(x.l, read.l, amplitude), .r = add_level<Level>(x.r, read.r, amplitude)};
	}

	/// @brief Feeds the history's taps at @p p, or silence where @p written is false.
	template <typename Feed>
	[[gnu::always_inline]] static void put_tap(Feed& feed, const InputHistory<Frame>& history, RingPosition p,
	                                           bool written) {
		// Words: the interpolator's taps are 16-bit words
		const int16_t l = written ? history.high_l(p).raw() : int16_t{0};
		if constexpr (std::is_same_v<Frame, StereoSample>) {
			feed.put(l, written ? history.high_r(p).raw() : int16_t{0});
		}
		else {
			feed.put(l);
		}
	}
};

static_assert(processor_kernel<PlayHead<Q<31>>>);
static_assert(processor_kernel<PlayHead<StereoSample>>);
static_assert(!std::is_polymorphic_v<PlayHead<Q<31>>>);

} // namespace deluge::dsp::fixed::pitch_shift
