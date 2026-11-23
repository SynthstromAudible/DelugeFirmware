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

#include "dsp/core/chain.h"
#include "dsp/core/fixed_point.h"
#include "dsp/core/fixed_point_neon.h"
#include "dsp/core/kernel.h"
#include "dsp/core/stereo_sample.h"
#include "dsp/core/types.h"
#include "dsp/fixed/filter/shift_tracker.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

/// @file
/// @brief Freeverb: eight combs and four allpasses per channel, as one block-staged kernel of stereo frames.
namespace deluge::dsp::fixed {

/// @brief Freeverb as one block-staged kernel of stereo frames, rendered in place over the master block as
/// `Mono<Freeverb>`: each frame takes the next sample of a mono send, blocks its DC, runs it through each channel's
/// combs in parallel and allpasses in series, and adds the result to the frame at the owner's two return levels.
/// @details The lines live in memory the owner hands in (@ref set_memory), kMemoryWords words laid out by
/// @ref kLayout; the State holds the lines' indices and the combs' filter stores. The send and the return levels
/// arrive each block by @ref set_send.
///
/// **The block.** @ref process runs the frames four at a time in NEON, one group of four lines per vector, and the last
/// frames of a block that is not a multiple of four one at a time (@ref frame). The comb lines do not feed each other,
/// and no allpass line is read back within a block, so each group runs straight through the block and gives exactly
/// what frame-by-frame order gives. A line that wraps during the block runs on into the kOverhang words after its end:
/// first they take a copy of the line's start, and afterwards the copy goes back.
///
/// **Exactness.** Every comb's filter store and each cross-mixed channel is even (a Q30 sum lifted to Q31), so the
/// kernel multiplies half of it with `vqrdmulh`, which gives exactly the scalar's rounded high word and cannot saturate
/// on it. The line samples and the allpasses' outputs, which need not be even, take the full rounded multiply.
struct Freeverb {
	using value_type = StereoSample; ///< A frame, Q31 per channel.
	using step_type = StereoSample;  ///< One frame per step.

	static constexpr std::size_t kCombs = 8;      ///< Combs per channel, summed in parallel.
	static constexpr std::size_t kAllpasses = 4;  ///< Allpasses per channel, in series.
	static constexpr uint32_t kStereoSpread = 23; ///< Each right line is this many words longer than its left.
	static constexpr float kScaleRoom = 0.28f;    ///< Room knob to comb feedback: feedback = room × 0.28 + 0.7.
	static constexpr float kOffsetRoom = 0.7f;
	static constexpr float kScaleDamp = 0.4f; ///< Damping knob to the combs' low-pass: damp = damping × 0.4.

	/// @brief The words after each line that a block runs on into: the most frames @ref process takes in one pass.
	static constexpr std::size_t kOverhang = 128;

	/// @brief One delay line in the memory: where it starts and how many words it plays (kOverhang more follow it).
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
	/// @brief The lines end to end, each followed by kOverhang words: the combs left then right, then the allpasses
	/// left then right, at Jezar's tunings for 44.1 kHz (reverb_ref::ReferenceFreeverb in
	/// tests/unit/dsp/reference_reverb.h), each right line kStereoSpread longer.
	static constexpr Layout kLayout = [] consteval {
		constexpr uint32_t kCombLengths[kCombs]{1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617}; // NOLINT
		constexpr uint32_t kAllpassLengths[kAllpasses]{556, 441, 341, 225};                      // NOLINT
		Layout l{};
		uint32_t base = 0;
		for (uint32_t ch = 0; ch < 2; ++ch) {
			for (std::size_t i = 0; i < kCombs; ++i) {
				l.comb[ch][i] = {base, kCombLengths[i] + (ch * kStereoSpread)};
				base += l.comb[ch][i].length + kOverhang;
			}
		}
		for (uint32_t ch = 0; ch < 2; ++ch) {
			for (std::size_t i = 0; i < kAllpasses; ++i) {
				l.allpass[ch][i] = {base, kAllpassLengths[i] + (ch * kStereoSpread)};
				base += l.allpass[ch][i].length + kOverhang;
			}
		}
		l.words = base;
		return l;
	}();
	/// The lines' 25,450 words and 24 overhangs of kOverhang: 28,522 words.
	static constexpr std::size_t kMemoryWords = kLayout.words;

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
		Q<31> comb_store[kCombs]{};          // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
		int32_t allpass_index[kAllpasses]{}; // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
	};
	/// @brief The owner's memory, this block's send cursor and return levels, the DC blocker and both channels.
	struct State {
		Q<31>* memory = nullptr;     ///< kMemoryWords words, laid out by kLayout.
		const Q<31>* send = nullptr; ///< The next send sample; set by set_send, advanced per frame.
		Q<31> pan_l{};               ///< The left return level.
		Q<31> pan_r{};               ///< The right return level.
		DcBlocker::State dc{};
		Channel left{};
		Channel right{};
	};

	/// @return The coefficients for @p s, in float, each converted to Q31 as the A9's VCVT does: truncating,
	/// saturating.
	/// @note Width 0 makes wet2 2^31, one past Q31: it converts to the largest Q31 on every target.
	[[nodiscard]] static Coefficients coefficients(const Settings& s) {
		const float roomsize = (s.room_size * kScaleRoom) + kOffsetRoom;
		const Q<31> damp1 = from_float<31>(s.damping * kScaleDamp);
		// Each channel's share of the other: half the width's shortfall over the channel's own share, which runs from a
		// half (width 0) to all of it (width 1).
		const float cross = (1.f - s.width) / 2;
		const float own = (s.width / 2) + 0.5f;
		return {
		    .feedback = from_float<31>(roomsize),
		    .damp1 = damp1,
		    .damp2 = sub_wrap(Q<31>::max(), damp1),
		    .wet2 = from_float<31>(cross / own),
		    .dc = {},
		};
	}

	/// @brief A block: the send's next samples, DC-blocked, through both channels, added to @p in into @p out.
	/// @param c The coefficients.
	/// @param s The State; its send advances by the block's length.
	/// @param in The frames the reverb adds to; may be @p out.
	/// @param out The frames written.
	static void process(const Coefficients& c, State& s, dsp::Signal<StereoSample> in, dsp::Buffer<StereoSample> out) {
		for (std::size_t done = 0; done < out.size();) {
			const std::size_t n = std::min(kOverhang, out.size() - done);
			const std::size_t vectored = n & ~std::size_t{3};
			if (vectored != 0) {
				pass(c, s, in.subspan(done, vectored), out.subspan(done, vectored));
			}
			for (std::size_t i = vectored; i < n; ++i) {
				out[done + i] = frame(c, s, in[done + i]);
			}
			done += n;
		}
	}

	/// @brief Event: the owner's memory, kMemoryWords words, zeroed by the owner.
	static void set_memory(State& s, Q<31>* memory) { s.memory = memory; }
	/// @brief Event: this block's send, a sample per frame rendered, and the two return levels.
	static void set_send(State& s, const Q<31>* send, Q<31> pan_l, Q<31> pan_r) {
		s.send = send;
		s.pan_l = pan_l;
		s.pan_r = pan_r;
	}

private:
	using Vec = QVec<31>;     ///< Four Q31 lanes: four frames of one line, or one frame of four lines.
	using HalfVec = QVec<30>; ///< Four even Q31 values, each held halved.

	/// @brief One frame: the send's next sample, DC-blocked, through both channels, added to @p x.
	[[nodiscard]] [[gnu::always_inline]] static StereoSample frame(const Coefficients& c, State& s, StereoSample x) {
		const Q<31> input = kernel_ops::step_at<DcBlocker, kernel_ops::NoMode::NONE>(c.dc, EmptyBlock{}, s.dc, *s.send);
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
		return {.l = add_wrap(x.l, reinterpret_bits<Q<31>>(mul_r(out_l, s.pan_l))),
		        .r = add_wrap(x.r, reinterpret_bits<Q<31>>(mul_r(out_r, s.pan_r)))};
	}

	/// @brief One pass of @p n frames, a multiple of 4 and at most kOverhang: the send DC-blocked, each group of four
	/// combs over the pass, each channel's allpasses, then the cross-mix and the return, as @ref frame gives frame by
	/// frame.
	static void pass(const Coefficients& c, State& s, dsp::Signal<StereoSample> in, dsp::Buffer<StereoSample> out) {
		const std::size_t n = out.size();
		alignas(16) std::array<Q<31>, kOverhang> send{};
		DcBlocker::State dc = s.dc;
		for (std::size_t i = 0; i < n; ++i) {
			send[i] = kernel_ops::step_at<DcBlocker, kernel_ops::NoMode::NONE>(c.dc, EmptyBlock{}, dc, s.send[i]);
		}
		s.dc = dc;
		s.send += n;

		for_each_line(s, [n, &s](Line line, int32_t& index) { start_line(s.memory, line, index, n); });

		alignas(16) std::array<Q<31>, kOverhang> left{};
		alignas(16) std::array<Q<31>, kOverhang> right{};
		four_combs<false>(c, s.memory, kLayout.comb[0].data(), s.left.comb_index, s.left.comb_store, send.data(),
		                  left.data(), n);
		four_combs<true>(c, s.memory, kLayout.comb[0].data() + 4, s.left.comb_index + 4, s.left.comb_store + 4,
		                 send.data(), left.data(), n);
		four_combs<false>(c, s.memory, kLayout.comb[1].data(), s.right.comb_index, s.right.comb_store, send.data(),
		                  right.data(), n);
		four_combs<true>(c, s.memory, kLayout.comb[1].data() + 4, s.right.comb_index + 4, s.right.comb_store + 4,
		                 send.data(), right.data(), n);
		static_assert(kCombs == 8 && kAllpasses == 4);
		four_allpasses(s.memory, kLayout.allpass[0].data(), s.left.allpass_index, left.data(), n);
		four_allpasses(s.memory, kLayout.allpass[1].data(), s.right.allpass_index, right.data(), n);

		for_each_line(s, [n, &s](Line line, int32_t& index) { end_line(s.memory, line, index, n); });

		// The cross-mix and the return's products, four frames at a time. The mixed channels are lifted Q30 sums, so
		// even: their halves take the exact vqrdmulh. The allpasses' right output takes the full multiply. The
		// reinterpretations are frame()'s: each channel's own output read as Q30 in its cross-mix (cross_mix), and the
		// return's Q30 product added to the Q31 frame as its bits.
		for (std::size_t i = 0; i < n; i += 4) {
			const Vec l = Vec::load(&left[i]);
			const Vec r = Vec::load(&right[i]);
			const Vec mixed_l = as_wrap<Vec>(add_wrap(reinterpret_bits<HalfVec>(l), mul_r(r, Vec::splat(c.wet2))));
			const auto half_l = as_floor<HalfVec>(mixed_l);
			const Vec mixed_r =
			    as_wrap<Vec>(add_wrap(reinterpret_bits<HalfVec>(r), mul_doubling_r_sat(half_l, c.wet2)));
			const auto half_r = as_floor<HalfVec>(mixed_r);
			reinterpret_bits<Vec>(mul_doubling_r_sat(half_l, s.pan_l)).store(&left[i]);
			reinterpret_bits<Vec>(mul_doubling_r_sat(half_r, s.pan_r)).store(&right[i]);
		}
		for (std::size_t i = 0; i < n; ++i) {
			out[i] = {.l = add_wrap(in[i].l, left[i]), .r = add_wrap(in[i].r, right[i])};
		}
	}

	/// @brief Calls @p f with each line's place and index, combs then allpasses.
	template <class F>
	[[gnu::always_inline]] static void for_each_line(State& s, const F& f) {
		for (std::size_t i = 0; i < kCombs; ++i) {
			f(kLayout.comb[0][i], s.left.comb_index[i]);
			f(kLayout.comb[1][i], s.right.comb_index[i]);
		}
		for (std::size_t i = 0; i < kAllpasses; ++i) {
			f(kLayout.allpass[0][i], s.left.allpass_index[i]);
			f(kLayout.allpass[1][i], s.right.allpass_index[i]);
		}
	}
	/// @brief Before a pass of @p n frames: a line that will run past its end gets a copy of its start there.
	static void start_line(Q<31>* memory, Line line, int32_t index, std::size_t n) {
		const auto run_on =
		    static_cast<std::ptrdiff_t>(static_cast<std::size_t>(index) + n) - static_cast<std::ptrdiff_t>(line.length);
		if (run_on > 0) {
			Q<31>* const begin = memory + line.base;
			std::copy_n(begin, run_on, begin + line.length);
		}
	}
	/// @brief After a pass of @p n frames: the index moved on, and a line that ran past its end has what it wrote there
	/// copied back to its start.
	static void end_line(Q<31>* memory, Line line, int32_t& index, std::size_t n) {
		index += static_cast<int32_t>(n);
		if (index >= static_cast<int32_t>(line.length)) {
			Q<31>* const begin = memory + line.base;
			index -= static_cast<int32_t>(line.length);
			std::copy_n(begin + line.length, index, begin);
		}
	}

	/// @brief Four combs, one per vector, over @p n frames (a multiple of 4): their outputs summed into @p out, or
	/// added to it with @p kAccumulate; the send fed back with their low-passed outputs. As @ref comb, four frames at a
	/// time, transposed so the filter stores step frame by frame across the lanes.
	template <bool kAccumulate>
	[[gnu::always_inline]] static void four_combs(const Coefficients& c, Q<31>* memory, const Line* lines,
	                                              const int32_t* index, Q<31>* store, const Q<31>* __restrict__ send,
	                                              Q<31>* __restrict__ out, std::size_t n) {
		Q<31>* __restrict__ p0 = memory + lines[0].base + index[0];
		Q<31>* __restrict__ p1 = memory + lines[1].base + index[1];
		Q<31>* __restrict__ p2 = memory + lines[2].base + index[2];
		Q<31>* __restrict__ p3 = memory + lines[3].base + index[3];
		const Vec damp2 = Vec::splat(c.damp2);
		auto half_store = as_floor<HalfVec>(Vec::load(store));
		for (std::size_t i = 0; i < n; i += 4) {
			// One comb per vector: what each outputs for the four frames
			Vec a = Vec::load(p0);
			Vec b = Vec::load(p1);
			Vec d0 = Vec::load(p2);
			Vec d1 = Vec::load(p3);
			Vec sum = add_wrap(add_wrap(a, b), add_wrap(d0, d1));
			if constexpr (kAccumulate) {
				sum = add_wrap(sum, Vec::load(out + i));
			}
			sum.store(out + i);

			// One frame per vector, to step the four filter stores frame by frame
			transpose(a, b, d0, d1);
			auto step = [&] [[gnu::always_inline]] (Vec & x) {
				const Vec stored = as_wrap<Vec>(add_wrap(mul_r(x, damp2), mul_doubling_r_sat(half_store, c.damp1)));
				half_store = as_floor<HalfVec>(stored);
				x = as_wrap<Vec>(mul_doubling_r_sat(half_store, c.feedback));
			};
			step(a);
			step(b);
			step(d0);
			step(d1);
			// Back to one comb per vector: the send plus each one's feedback into its line
			transpose(a, b, d0, d1);
			const Vec input = Vec::load(send + i);
			add_wrap(input, a).store(p0);
			add_wrap(input, b).store(p1);
			add_wrap(input, d0).store(p2);
			add_wrap(input, d1).store(p3);
			p0 += 4;
			p1 += 4;
			p2 += 4;
			p3 += 4;
		}
		as_wrap<Vec>(half_store).store(store);
	}

	/// @brief A channel's four allpasses in series over @p n frames (a multiple of 4) of @p samples, in place: as
	/// @ref allpass, four frames at a time, which no line reads back within the pass.
	[[gnu::always_inline]] static void four_allpasses(Q<31>* memory, const Line* lines, const int32_t* index,
	                                                  Q<31>* __restrict__ samples, std::size_t n) {
		std::array<Q<31>*, kAllpasses> p{};
		for (std::size_t k = 0; k < kAllpasses; ++k) {
			p[k] = memory + lines[k].base + index[k];
		}
		for (std::size_t i = 0; i < n; i += 4) {
			Vec x = Vec::load(samples + i);
			for (std::size_t k = 0; k < kAllpasses; ++k) {
				const Vec bufout = Vec::load(p[k] + i);
				add_scale_shr_floor<1>(x, bufout).store(p[k] + i);
				x = sub_wrap(bufout, x);
			}
			x.store(samples + i);
		}
	}

	/// @return @p own read as Q30 plus @p other's share at @p wet2, lifted to Q31: 2 × own + wet2 × other, wrapping.
	[[nodiscard]] [[gnu::always_inline]] static Q<31> cross_mix(Q<31> own, Q<31> other, Q<31> wet2) {
		// The allpasses' output's bits read as Q30, to add the Q30 share at its scale.
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
	[[nodiscard]] [[gnu::always_inline]] static Q<31> comb(const Coefficients& c, Q<31>* memory, Line line,
	                                                       int32_t& index, Q<31>& store, Q<31> input) {
		Q<31>* const slot = memory + line.base + index;
		const Q<31> output = *slot;
		// The low-pass: two Q30 products, their sum lifted to the Q31 store; the feedback's product lifted likewise.
		const auto stored = as_wrap<Q<31>>(add_wrap(mul_r(output, c.damp2), mul_r(store, c.damp1)));
		store = stored;
		*slot = add_wrap(input, mul_r_as_wrap<Q<31>>(stored, c.feedback));
		if (++index >= static_cast<int32_t>(line.length)) {
			index = 0;
		}
		return output;
	}
	/// @brief One allpass: the tail less the input out; the input plus half the tail in (a feedback of one half, as a
	/// shift).
	[[nodiscard]] [[gnu::always_inline]] static Q<31> allpass(Q<31>* memory, Line line, int32_t& index, Q<31> input) {
		Q<31>* const slot = memory + line.base + index;
		const Q<31> bufout = *slot;
		const Q<31> output = sub_wrap(bufout, input);
		*slot = add_wrap(input, halve_floor(bufout));
		if (++index >= static_cast<int32_t>(line.length)) {
			index = 0;
		}
		return output;
	}
};

static_assert(staged_kernel<Freeverb>);
static_assert(!std::is_polymorphic_v<Freeverb>);
static_assert(Freeverb::kMemoryWords == 25450 + (24 * Freeverb::kOverhang));

} // namespace deluge::dsp::fixed
