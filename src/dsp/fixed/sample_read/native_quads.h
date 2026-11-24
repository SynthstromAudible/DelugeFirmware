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
#include "dsp/fixed/sample_read/frame_source.h"

#include <arm_neon.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace deluge::dsp::fixed::sample_read {

/// @file
/// @brief The native read's frames four at a time (R7): each sample its bytes at the top of a word, zeros below, as the
/// 32-bit read ending at its top byte masked by its depth gives it.
///
/// Every load is a byte form (vld1.8, vld3.8): a sample may start at any byte, and a wider lane load carries its
/// element's alignment, which faults at an odd address. A pass reads only the bytes of its own frames.

namespace detail {

/// @p v as it is, opaque to the optimiser, so the shuffles either side of it are not fused into one the ARM backend
/// lowers to single-lane moves through the core registers.
[[gnu::always_inline]] inline uint32x4_t apart(uint32x4_t v) {
#ifdef __arm__
	asm("" : "+w"(v));
#endif
	return v;
}

/// Lanes 3 to 0 of @p v.
[[gnu::always_inline]] inline uint32x4_t reversed4(uint32x4_t v) {
	const uint32x4_t r = vrev64q_u32(apart(v));
	return vcombine_u32(vget_high_u32(r), vget_low_u32(r));
}

/// One pass's words in memory order: four frames' (l0, r0), or for 8-, 16- and 24-bit mono eight frames' (l0 then l1).
struct NativeWords {
	uint32x4_t l0, r0, l1;
};

template <int Depth, int Channels>
inline constexpr int32_t kNativePassFrames = (Channels == 1 && Depth != 4) ? 8 : 4;

/// Eight 24-bit samples' words from their low, middle and high byte planes: each `lo << 8 | mid << 16 | hi << 24`.
[[gnu::always_inline]] inline void words24(uint8x8x3_t planes, uint32x4_t& first, uint32x4_t& second) {
	const uint8x8x2_t low = vzip_u8(vdup_n_u8(0), planes.val[0]);
	const uint8x8x2_t high = vzip_u8(planes.val[1], planes.val[2]);
	const uint16x4x2_t a = vzip_u16(vreinterpret_u16_u8(low.val[0]), vreinterpret_u16_u8(high.val[0]));
	const uint16x4x2_t b = vzip_u16(vreinterpret_u16_u8(low.val[1]), vreinterpret_u16_u8(high.val[1]));
	first = apart(vcombine_u32(vreinterpret_u32_u16(a.val[0]), vreinterpret_u32_u16(a.val[1])));
	second = apart(vcombine_u32(vreinterpret_u32_u16(b.val[0]), vreinterpret_u32_u16(b.val[1])));
}

/// One pass of frames from @p at, the first sample byte of the pass's lowest frame in memory.
template <int Depth, int Channels>
[[gnu::always_inline]] inline NativeWords native_pass(const uint8_t* at) {
	NativeWords w{};
	if constexpr (Channels == 2) {
		uint32x4_t first;
		uint32x4_t second;
		if constexpr (Depth == 4) {
			first = vreinterpretq_u32_u8(vld1q_u8(at));
			second = vreinterpretq_u32_u8(vld1q_u8(at + 16));
		}
		else if constexpr (Depth == 3) {
			words24(vld3_u8(at), first, second);
		}
		else if constexpr (Depth == 2) {
			const uint16x8_t h = vreinterpretq_u16_u8(vld1q_u8(at));
			const uint16x4x2_t lr = vuzp_u16(vget_low_u16(h), vget_high_u16(h));
			w.l0 = vshll_n_u16(lr.val[0], 16);
			w.r0 = vshll_n_u16(lr.val[1], 16);
			return w;
		}
		else {
			const uint8x8_t b = vld1_u8(at);
			const uint8x8x2_t lr = vuzp_u8(b, b);
			w.l0 = vshlq_n_u32(vshll_n_u16(vget_low_u16(vmovl_u8(lr.val[0])), 16), 8);
			w.r0 = vshlq_n_u32(vshll_n_u16(vget_low_u16(vmovl_u8(lr.val[1])), 16), 8);
			return w;
		}
		// Words alternate left and right: split them
		const uint32x4x2_t lr = vuzpq_u32(first, second);
		w.l0 = lr.val[0];
		w.r0 = lr.val[1];
	}
	else if constexpr (Depth == 4) {
		w.l0 = vreinterpretq_u32_u8(vld1q_u8(at));
	}
	else if constexpr (Depth == 3) {
		words24(vld3_u8(at), w.l0, w.l1);
	}
	else if constexpr (Depth == 2) {
		const uint16x8_t h = vreinterpretq_u16_u8(vld1q_u8(at));
		w.l0 = vshll_n_u16(vget_low_u16(h), 16);
		w.l1 = vshll_n_u16(vget_high_u16(h), 16);
	}
	else {
		const uint16x8_t h = vmovl_u8(vld1_u8(at));
		w.l0 = vshlq_n_u32(vshll_n_u16(vget_low_u16(h), 16), 8);
		w.l1 = vshlq_n_u32(vshll_n_u16(vget_high_u16(h), 16), 8);
	}
	return w;
}

[[gnu::always_inline]] inline QVec<31> as_samples(uint32x4_t words) {
	// A masked word is the sample's word, read as Q31 as the native read reads it
	return QVec<31>::from_raw(vreinterpretq_s32_u32(words));
}

/// One pass's words into @p sink in play order: as they lie going forward, lanes and halves reversed going back.
template <int kPass, int Channels, class Sink>
[[gnu::always_inline]] inline void put_pass(const NativeWords& w, bool forward, Sink& sink) {
	if (forward) {
		sink.put(as_samples(w.l0), as_samples(Channels == 2 ? w.r0 : w.l0), 4);
		if constexpr (kPass == 8) {
			sink.put(as_samples(w.l1), as_samples(w.l1), 4);
		}
	}
	else if constexpr (kPass == 8) {
		sink.put(as_samples(reversed4(w.l1)), as_samples(reversed4(w.l1)), 4);
		sink.put(as_samples(reversed4(w.l0)), as_samples(reversed4(w.l0)), 4);
	}
	else {
		sink.put(as_samples(reversed4(w.l0)), as_samples(reversed4(Channels == 2 ? w.r0 : w.l0)), 4);
	}
}

/// Frames @p k to @p frames − 1 of @p run, a word each through the scalar read's masked 32-bit read, mixed one at a
/// time in core registers.
template <int Depth, int Channels, class Sink>
void native_tail(const FrameRun& run, int32_t k, int32_t frames, Sink& sink) {
	// The native read is word arithmetic (R7), a sample's bytes masked from a word, so these crossings are those words
	constexpr uint32_t kMask = 0xFFFF'FFFFU << (32 - (8 * Depth));
	for (; k < frames; k++) {
		const std::byte* at = run.first + (static_cast<std::ptrdiff_t>(k) * run.stride);
		uint32_t l = 0;
		uint32_t r = 0;
		std::memcpy(&l, at, sizeof l);
		if constexpr (Channels == 2) {
			std::memcpy(&r, at + Depth, sizeof r);
		}
		sink.put_one(Q<31>::from_raw(static_cast<int32_t>(l & kMask)),
		             Q<31>::from_raw(static_cast<int32_t>(r & kMask)));
	}
}

template <int Depth, int Channels, class Sink>
void native_as(const FrameRun& run, int32_t frames, Sink& sink) {
	constexpr int32_t kPass = kNativePassFrames<Depth, Channels>;
	const auto* origin = reinterpret_cast<const uint8_t*>(run.first);
	const bool forward = run.stride > 0;
	// Frame k's first sample byte: its 32-bit read's origin plus the bytes below its top
	auto sample = [&] [[gnu::always_inline]] (int32_t k) {
		return origin + (static_cast<std::ptrdiff_t>(k) * run.stride) + 4 - Depth;
	};
	int32_t k = 0;
	// Two passes' loads ahead of their mixes, so one's latencies fill with the other's work
	for (; k + (2 * kPass) <= frames; k += 2 * kPass) {
		const NativeWords a = native_pass<Depth, Channels>(sample(forward ? k : k + kPass - 1));
		const NativeWords b = native_pass<Depth, Channels>(sample(forward ? k + kPass : k + (2 * kPass) - 1));
		put_pass<kPass, Channels>(a, forward, sink);
		put_pass<kPass, Channels>(b, forward, sink);
	}
	for (; k + kPass <= frames; k += kPass) {
		put_pass<kPass, Channels>(native_pass<Depth, Channels>(sample(forward ? k : k + kPass - 1)), forward, sink);
	}
	native_tail<Depth, Channels>(run, k, frames, sink);
}

} // namespace detail

/// @brief Mixes @p frames native frames of @p run, from its first, into @p sink four at a time
/// (`sink.put(l, r, count)`; @p r repeats @p l for a mono run).
template <int Channels, class Sink>
void native_quads(const FrameRun& run, int32_t frames, Sink& sink) {
	switch (run.byte_depth) {
	case 1:
		detail::native_as<1, Channels>(run, frames, sink);
		break;
	case 2:
		detail::native_as<2, Channels>(run, frames, sink);
		break;
	case 3:
		detail::native_as<3, Channels>(run, frames, sink);
		break;
	default:
		detail::native_as<4, Channels>(run, frames, sink);
		break;
	}
}

} // namespace deluge::dsp::fixed::sample_read
