// Freeverb, written by Jezar at Dreampoint, June 2000
// http://www.dreampoint.co.uk
// This code is public domain

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

#include "dsp-next/core/chain.h"
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/filter/shift_tracker.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

/// @file
/// @brief Freeverb: eight combs and four allpasses per channel, as one processor kernel of stereo frames.
namespace deluge::dsp::fixed {

/// @brief Freeverb as one processor kernel of stereo frames, rendered in place over the master block as
/// `Mono<Freeverb>`: each frame takes the next sample of a mono send, blocks its DC, runs it through each channel's
/// combs in parallel and allpasses in series, and adds the result to the frame at the owner's two return levels.
/// @details The lines live in memory the owner hands in (@ref set_memory), kMemoryWords words laid out by
/// @ref kLayout; the State holds the lines' indices and the combs' filter stores. The send and the return levels
/// arrive each block by @ref set_send.
struct Freeverb {
	using value_type = StereoSample; ///< A frame, Q31 per channel.
	using step_type = StereoSample;  ///< One frame per step.

	static constexpr std::size_t kCombs = 8;      ///< Combs per channel, summed in parallel.
	static constexpr std::size_t kAllpasses = 4;  ///< Allpasses per channel, in series.
	static constexpr uint32_t kStereoSpread = 23; ///< Each right line is this many words longer than its left.
	static constexpr float kScaleRoom = 0.28f;    ///< Room knob to comb feedback: feedback = room × 0.28 + 0.7.
	static constexpr float kOffsetRoom = 0.7f;
	static constexpr float kScaleDamp = 0.4f; ///< Damping knob to the combs' low-pass: damp = damping × 0.4.

	/// @brief One delay line in the memory: where it starts and how many words it holds.
	struct Line {
		uint32_t base;
		uint32_t length;
	};
	/// @brief Every line's place in the memory, left channel at [0], and the words they take.
	struct Layout {
		std::array<std::array<Line, kCombs>, 2> comb;
		std::array<std::array<Line, kAllpasses>, 2> allpass;
		uint32_t words;
	};
	/// @brief The lines end to end: the combs left then right, then the allpasses left then right, at Jezar's tunings
	/// for 44.1 kHz (reverb_ref::ReferenceFreeverb in tests/dsp/reference_reverb.h), each right line kStereoSpread
	/// longer.
	static constexpr Layout kLayout = [] consteval {
		constexpr uint32_t kCombLengths[kCombs]{1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617}; // NOLINT
		constexpr uint32_t kAllpassLengths[kAllpasses]{556, 441, 341, 225};                      // NOLINT
		Layout l{};
		uint32_t base = 0;
		for (uint32_t ch = 0; ch < 2; ++ch) {
			for (std::size_t i = 0; i < kCombs; ++i) {
				l.comb[ch][i] = {base, kCombLengths[i] + (ch * kStereoSpread)};
				base += l.comb[ch][i].length;
			}
		}
		for (uint32_t ch = 0; ch < 2; ++ch) {
			for (std::size_t i = 0; i < kAllpasses; ++i) {
				l.allpass[ch][i] = {base, kAllpassLengths[i] + (ch * kStereoSpread)};
				base += l.allpass[ch][i].length;
			}
		}
		l.words = base;
		return l;
	}();
	static constexpr std::size_t kMemoryWords = kLayout.words; ///< 25,450 words.

	/// @brief The send's DC blocker: a one-pole high-pass at shift 11, y += (x − y) >> 11 and x − y out.
	using DcBlocker = Pinned<ShiftTracker<11>, ShiftTracker<11>::Mode::REMOVE>;

	/// @brief What the owner sets: the knobs, each 0 to 1.
	struct Settings {
		float room_size = 0.5f; ///< Freeverb's initialroom.
		float damping = 0.5f;   ///< Freeverb's initialdamp.
		float width = 1.f;      ///< Freeverb's initialwidth.
		bool operator==(const Settings&) const = default;
	};
	/// @brief What the arithmetic reads.
	struct Coefficients {
		Q<31> feedback{}; ///< Each comb's feedback.
		Q<31> damp1{};    ///< The combs' low-pass memory weight.
		Q<31> damp2{};    ///< The combs' low-pass input weight, max − damp1.
		Q<31> wet2{};     ///< Each channel's share of the other: 0 at full width.
		DcBlocker::Coefficients dc{};
	};
	/// @brief One channel's indices into its lines and its combs' filter stores.
	struct Channel {
		int32_t comb_index[kCombs]{};        // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
		q31 comb_store[kCombs]{};            // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
		int32_t allpass_index[kAllpasses]{}; // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
	};
	/// @brief The owner's memory, this block's send cursor and return levels, the DC blocker and both channels.
	struct State {
		q31* memory = nullptr;     ///< kMemoryWords words, laid out by kLayout.
		const q31* send = nullptr; ///< The next send sample; set by set_send, advanced per frame.
		q31 pan_l = 0;             ///< The left return level, Q31.
		q31 pan_r = 0;             ///< The right return level, Q31.
		DcBlocker::State dc{};
		Channel left{};
		Channel right{};
	};

	/// @return The coefficients for @p s, in float, each product converted to Q31 by a plain float-to-int conversion.
	/// @note Width 0 makes wet2's product 2^31, out of int32's range: the conversion is the targets' own (x86 gives
	/// INT32_MIN, ARM saturates to INT32_MAX), kept as it is.
	[[nodiscard]] static Coefficients coefficients(const Settings& s) {
		constexpr auto kMax = std::numeric_limits<int32_t>::max();
		const float roomsize = (s.room_size * kScaleRoom) + kOffsetRoom;
		const float damp = s.damping * kScaleDamp;
		const auto damp1 = static_cast<q31>(damp * static_cast<float>(kMax)); // kMax rounds up to 2^31 as a float
		// Each channel's share of the other: half the width's shortfall over the channel's own share, which runs from a
		// half (width 0) to all of it (width 1).
		const float cross = (1.f - s.width) / 2;
		const float own = (s.width / 2) + 0.5f;
		return {
		    .feedback = Q<31>::from_raw(static_cast<q31>(roomsize * static_cast<float>(kMax))),
		    .damp1 = Q<31>::from_raw(damp1),
		    .damp2 = sub_wrap(Q<31>::max(), Q<31>::from_raw(damp1)),
		    .wet2 = Q<31>::from_raw(static_cast<q31>(cross / own * static_cast<float>(kMax))),
		    .dc = {},
		};
	}

	/// @brief One frame: the send's next sample, DC-blocked, through both channels, added to @p x.
	[[nodiscard]] [[gnu::always_inline]] static StereoSample step(const Coefficients& c, const EmptyBlock& /*b*/,
	                                                              State& s, StereoSample x) {
		const auto input = Q<31>::from_raw(
		    kernel_ops::step_at<DcBlocker, kernel_ops::NoMode::NONE>(c.dc, EmptyBlock{}, s.dc, *s.send));
		++s.send;
		Q<31> out_l{};
		Q<31> out_r{};
		combs(c, s, input, out_l, out_r, std::make_index_sequence<kCombs>{});
		allpasses(s, out_l, out_r, std::make_index_sequence<kAllpasses>{});
		// The cross-mix sums at Q30, each channel's own output read as Q30 (twice its value) plus its Q30 share of the
		// other, then lifts back to Q31. Named quirk: out_r takes in the out_l this frame has just made, not the
		// allpasses' left output.
		out_l = cross_mix(out_l, out_r, c.wet2);
		out_r = cross_mix(out_r, out_l, c.wet2);
		// The return adds the Q30 product's bits to the Q31 frame: half the output at the return level.
		return {
		    .l = add_wrap(Q<31>::from_raw(x.l), reinterpret_bits<Q<31>>(mul_r(out_l, Q<31>::from_raw(s.pan_l)))).raw(),
		    .r = add_wrap(Q<31>::from_raw(x.r), reinterpret_bits<Q<31>>(mul_r(out_r, Q<31>::from_raw(s.pan_r)))).raw()};
	}

	/// @brief Event: the owner's memory, kMemoryWords words, zeroed by the owner.
	static void set_memory(State& s, q31* memory) { s.memory = memory; }
	/// @brief Event: this block's send, a sample per frame rendered, and the two return levels (Q31).
	static void set_send(State& s, const q31* send, q31 pan_l, q31 pan_r) {
		s.send = send;
		s.pan_l = pan_l;
		s.pan_r = pan_r;
	}

private:
	/// @return @p own read as Q30 plus @p other's share at @p wet2, lifted to Q31: 2 × own + wet2 × other, wrapping.
	[[nodiscard]] [[gnu::always_inline]] static Q<31> cross_mix(Q<31> own, Q<31> other, Q<31> wet2) {
		// The comb sum's bits read as Q30, to add the Q30 share at its scale.
		return as_wrap<Q<31>>(add_wrap(reinterpret_bits<Q<30>>(own), mul_r(other, wet2)));
	}
	/// @brief Every comb of both channels, left then right for each, summed into @p out_l and @p out_r. Unrolled at
	/// compile time, so each line's base and length are immediates and each comb's index and store a fixed offset.
	template <std::size_t... I>
	[[gnu::always_inline]] static void combs(const Coefficients& c, State& s, Q<31> input, Q<31>& out_l, Q<31>& out_r,
	                                         std::index_sequence<I...> /*combs*/) {
		((out_l =
		      add_wrap(out_l, comb(c, s.memory, kLayout.comb[0][I], s.left.comb_index[I], s.left.comb_store[I], input)),
		  out_r = add_wrap(out_r,
		                   comb(c, s.memory, kLayout.comb[1][I], s.right.comb_index[I], s.right.comb_store[I], input))),
		 ...);
	}
	/// @brief Every allpass of both channels in series, left then right for each, unrolled as @ref combs.
	template <std::size_t... I>
	[[gnu::always_inline]] static void allpasses(State& s, Q<31>& out_l, Q<31>& out_r,
	                                             std::index_sequence<I...> /*allpasses*/) {
		((out_l = allpass(s.memory, kLayout.allpass[0][I], s.left.allpass_index[I], out_l),
		  out_r = allpass(s.memory, kLayout.allpass[1][I], s.right.allpass_index[I], out_r)),
		 ...);
	}
	/// @brief One comb: the line's tail out; its low-passed tail fed back with the input at its head.
	[[nodiscard]] [[gnu::always_inline]] static Q<31> comb(const Coefficients& c, q31* memory, Line line,
	                                                       int32_t& index, q31& store, Q<31> input) {
		q31* const slot = memory + line.base + index;
		const auto output = Q<31>::from_raw(*slot);
		// The low-pass: two Q30 products, their sum lifted to the Q31 store; the feedback's product lifted likewise.
		const auto stored = as_wrap<Q<31>>(add_wrap(mul_r(output, c.damp2), mul_r(Q<31>::from_raw(store), c.damp1)));
		store = stored.raw();
		*slot = add_wrap(input, mul_r_as_wrap<Q<31>>(stored, c.feedback)).raw();
		if (++index >= static_cast<int32_t>(line.length)) {
			index = 0;
		}
		return output;
	}
	/// @brief One allpass: the tail less the input out; the input plus half the tail in (a feedback of one half, as a
	/// shift).
	[[nodiscard]] [[gnu::always_inline]] static Q<31> allpass(q31* memory, Line line, int32_t& index, Q<31> input) {
		q31* const slot = memory + line.base + index;
		const auto bufout = Q<31>::from_raw(*slot);
		const Q<31> output = sub_wrap(bufout, input);
		*slot = add_wrap(input, halve_floor(bufout)).raw();
		if (++index >= static_cast<int32_t>(line.length)) {
			index = 0;
		}
		return output;
	}
};

static_assert(processor_kernel<Freeverb>);
static_assert(!std::is_polymorphic_v<Freeverb>);
static_assert(Freeverb::kMemoryWords == 25450);

} // namespace deluge::dsp::fixed
