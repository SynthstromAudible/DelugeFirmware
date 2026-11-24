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

#include "dsp/core/fixed_point.h"
#include "dsp/core/fixed_point_neon.h"
#include "dsp/fixed/sample_read/mixed_in.h"

#include <arm_neon.h>
#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::dsp::fixed::sample_read {

/// @file
/// @brief How a sample read mixes its frames four at a time (R7 to R9, W1 of docs/dev/time_stretcher_contracts.md):
/// the amplitude stepped before each frame, the cache's bytes, the condense of a stereo source to one channel, and
/// `mixed_in`, each the scalar per-frame mix's words exactly.
///
/// The four frames' amplitudes are the amplitude plus one to four steps, wrapping, as the scalar ramp's are; the mix is
/// the vector `mul_r` (`vmull.s32`, `vrshrn #32`), which is the scalar `smmlar`'s rounded high word.

/// @brief How a stereo source condenses to one output channel: not at all, the sinc read's halving toward zero, or the
/// linear and native reads' halving down.
enum class Condense : uint8_t { NONE, TOWARD_ZERO, FLOOR };

/// @brief A read's mix into @p out, four frames a call.
/// @tparam Channels The source's channels; @tparam OutChannels the output's, no more than the source's.
/// @tparam C The condense a stereo source to one channel takes.
template <int Channels, int OutChannels, Condense C>
class QuadMix {
	static_assert(OutChannels <= Channels);
	static_assert(Channels == 1 || OutChannels == 2 || C != Condense::NONE, "a stereo source to one channel condenses");

public:
	/// @param out The output's first frame. @param amplitude The amplitude before the first frame; @param step its
	/// step.
	/// @param cache Where the cache's bytes go, three per channel per frame; null writes none.
	QuadMix(Q<31>* out, Q<31> amplitude, Q<31> step, std::byte* cache)
	    : out_(out), amplitude_(amplitude), step_(step), cache_(cache) {
		// The four frames' steps as words, wrapping: lanes one to four steps on. The ramp is word arithmetic, as the
		// scalar add_wrap's is, so its crossings here are those words
		const auto s = static_cast<uint32_t>(step.raw());
		// One to four steps: the step times the lane numbers, made in registers
		const uint32x2_t ones = vcreate_u32((uint64_t{2} << 32) | 1u);
		const uint32x4_t lanes = vcombine_u32(ones, vadd_u32(ones, vdup_n_u32(2)));
		amps_ = vaddq_s32(vdupq_n_s32(amplitude.raw()), vreinterpretq_s32_u32(vmulq_n_u32(lanes, s)));
		step4_ = vdupq_n_s32(static_cast<int32_t>(4 * s));
	}

	/// @brief Mixes frames 0 to @p count − 1 of @p l and @p r (each lane a frame's sample); @p count is 4 but at a
	/// read's or a run's end.
	/// @details Inlined into the gather, so the ramp stays in its register: the next four amplitudes are a vector, and
	/// the scalar amplitude is core arithmetic, never a lane moved out.
	[[gnu::always_inline]] void put(QVec<31> l, QVec<31> r, int32_t count) {
		// The words: the samples are words, the voice's buffer a Q30 mix read as Q31
		int32x4_t lw = l.raw();
		const int32x4_t rw = r.raw();
		if (cache_ != nullptr) [[unlikely]] {
			write_cache(lw, rw, count);
		}
		if constexpr (Channels == 2 && OutChannels == 1) {
			lw = condensed(lw, rw);
		}
		if (count == 4) [[likely]] {
			mix4(lw, rw, amps_);
			amps_ = vaddq_s32(amps_, step4_);
		}
		else {
			mix_some(lw, rw, amps_, count);
			// A partial call may come mid-read (a run's end): the ramp moves on by the frames it mixed
			amps_ = vaddq_s32(amps_, vdupq_n_s32(static_cast<int32_t>(static_cast<uint32_t>(step_.raw())
			                                                          * static_cast<uint32_t>(count))));
		}
		amplitude_ = Q<31>::from_raw(
		    static_cast<int32_t>(static_cast<uint32_t>(amplitude_.raw())
		                         + (static_cast<uint32_t>(step_.raw()) * static_cast<uint32_t>(count))));
		out_ += static_cast<std::ptrdiff_t>(count) * OutChannels;
	}

	/// @brief Mixes one frame of @p l and @p r, its words in core registers: a run's last frames, which a vector group
	/// would move through memory twice. The ramp moves on by one step.
	[[gnu::always_inline]] void put_one(Q<31> l, Q<31> r) {
		amplitude_ = add_wrap(amplitude_, step_);
		if (cache_ != nullptr) [[unlikely]] {
			put_bytes(static_cast<uint32_t>(l.raw()));
			if constexpr (Channels == 2) {
				put_bytes(static_cast<uint32_t>(r.raw()));
			}
		}
		if constexpr (Channels == 2 && OutChannels == 1) {
			l = C == Condense::TOWARD_ZERO ? add_wrap(halve_toward_zero(l), halve_toward_zero(r))
			                               : add_wrap(halve_floor(l), halve_floor(r));
		}
		out_[0] = mixed_in(out_[0], l, amplitude_);
		if constexpr (OutChannels == 2) {
			out_[1] = mixed_in(out_[1], r, amplitude_);
		}
		out_ += OutChannels;
		amps_ = vaddq_s32(amps_, vdupq_n_s32(step_.raw()));
	}

	/// @brief The amplitude after the frames put.
	[[nodiscard]] Q<31> amplitude() const { return amplitude_; }
	/// @brief The cache's next byte, past the frames put; null when none is written.
	[[nodiscard]] std::byte* cache() const { return cache_; }

private:
	[[gnu::always_inline]] static int32x4_t condensed(int32x4_t l, int32x4_t r) {
		if constexpr (C == Condense::TOWARD_ZERO) {
			// x / 2: the sign bit added before the shift rounds a negative half toward zero
			auto half = [] [[gnu::always_inline]] (int32x4_t x) {
				return vshrq_n_s32(vaddq_s32(x, vreinterpretq_s32_u32(vshrq_n_u32(vreinterpretq_u32_s32(x), 31))), 1);
			};
			return vaddq_s32(half(l), half(r));
		}
		else {
			return vaddq_s32(vshrq_n_s32(l, 1), vshrq_n_s32(r, 1));
		}
	}

	/// The voice's buffer holds its mix at half scale: its words are read as Q30 so the Q31 product lands there, as
	/// mixed_in reads them, and these crossings are that reinterpretation.
	[[gnu::always_inline]] static int32x4_t mixed(int32x4_t mix, int32x4_t sample, int32x4_t amps) {
		const QVec<30> product = mul_r(QVec<31>::from_raw(sample), QVec<31>::from_raw(amps));
		return vaddq_s32(mix, product.raw());
	}

	[[gnu::always_inline]] void mix4(int32x4_t l, int32x4_t r, int32x4_t amps) {
		auto* words = reinterpret_cast<int32_t*>(out_);
		if constexpr (OutChannels == 2) {
			int32x4x2_t v = vld2q_s32(words);
			v.val[0] = mixed(v.val[0], l, amps);
			v.val[1] = mixed(v.val[1], r, amps);
			vst2q_s32(words, v);
		}
		else {
			vst1q_s32(words, mixed(vld1q_s32(words), l, amps));
		}
	}

	/// A read's last frames, fewer than four: the same words, frame by frame, so nothing past them is touched; the
	/// lanes come back as the words they hold.
	void mix_some(int32x4_t l, int32x4_t r, int32x4_t amps, int32_t count) {
		std::array<int32_t, 4> ls{};
		std::array<int32_t, 4> rs{};
		std::array<int32_t, 4> as{};
		vst1q_s32(ls.data(), l);
		vst1q_s32(rs.data(), r);
		vst1q_s32(as.data(), amps);
		for (int32_t f = 0; f < count; f++) {
			const Q<31> amplitude = Q<31>::from_raw(as[f]);
			out_[f * OutChannels] = mixed_in(out_[f * OutChannels], Q<31>::from_raw(ls[f]), amplitude);
			if constexpr (OutChannels == 2) {
				out_[(f * 2) + 1] = mixed_in(out_[(f * 2) + 1], Q<31>::from_raw(rs[f]), amplitude);
			}
		}
	}

	/// The cache's top three bytes of each frame's words, left then right. A cache is written only while a pitched
	/// sample is first played, so its bytes go one at a time from the lanes.
	[[gnu::noinline]] void write_cache(int32x4_t l, int32x4_t r, int32_t count) {
		std::array<uint32_t, 4> ls{};
		std::array<uint32_t, 4> rs{};
		vst1q_u32(ls.data(), vreinterpretq_u32_s32(l));
		vst1q_u32(rs.data(), vreinterpretq_u32_s32(r));
		for (int32_t f = 0; f < count; f++) {
			put_bytes(ls[f]);
			if constexpr (Channels == 2) {
				put_bytes(rs[f]);
			}
		}
	}

	void put_bytes(uint32_t w) {
		*cache_++ = static_cast<std::byte>(w >> 8);
		*cache_++ = static_cast<std::byte>(w >> 16);
		*cache_++ = static_cast<std::byte>(w >> 24);
	}

	Q<31>* out_;
	Q<31> amplitude_;
	Q<31> step_;
	std::byte* cache_;
	int32x4_t amps_;  ///< The next four frames' amplitudes, wrapping words.
	int32x4_t step4_; ///< Four steps, a word.
};

} // namespace deluge::dsp::fixed::sample_read
