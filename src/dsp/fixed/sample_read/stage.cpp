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
#include "dsp/fixed/sample_read/stage.h"

#include <arm_neon.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace deluge::dsp::fixed::sample_read {

namespace {

// Every load below is a byte form (vld1.8 to vld4.8): a sample may start at any byte, and a halfword or word lane
// load carries its element's alignment, which faults at an odd address however the core's alignment check is set.

/// The halfword at @p at, as a sample's top two bytes are read.
[[gnu::always_inline]] inline int16_t halfword(const uint8_t* at) {
	int16_t word = 0;
	std::memcpy(&word, at, sizeof word);
	return word;
}

/// Little-endian halfwords from a low-byte plane and a high-byte plane: 16 lanes, as two 8-lane vectors.
[[gnu::always_inline]] inline int16x8x2_t join(uint8x16_t low, uint8x16_t high) {
	const uint8x16x2_t z = vzipq_u8(low, high);
	return {{vreinterpretq_s16_u8(z.val[0]), vreinterpretq_s16_u8(z.val[1])}};
}

/// @p v as it is, but opaque to the optimiser: shuffles either side of it stay apart. Fused, an unzip and a lane
/// reversal become one shuffle the ARM backend lowers to single-lane moves through the core registers.
[[gnu::always_inline]] inline int16x8_t apart(int16x8_t v) {
#ifdef __arm__
	asm("" : "+w"(v));
#endif
	return v;
}

/// Lanes 7 to 0 of @p v.
[[gnu::always_inline]] inline int16x8_t reversed(int16x8_t v) {
	const int16x8_t r = vrev64q_s16(apart(v));
	return vcombine_s16(vget_high_s16(r), vget_low_s16(r));
}

/// One pass's taps: 16 frames' (two vectors per channel) or, for 24- and 32-bit stereo, 8 frames' (the first only).
struct PassTaps {
	int16x8_t l0, l1, r0, r1;
};

/// Frames one pass decodes from exactly their bytes (and at depth 1 the byte before them).
template <int Depth, int Channels>
inline constexpr int32_t kPassFrames = (Channels == 2 && (Depth == 3 || Depth == 4)) ? 8 : 16;

/// One pass of frames from @p at, their first sample byte.
template <int Depth, int Channels>
[[gnu::always_inline]] inline PassTaps pass(const uint8_t* at) {
	PassTaps p{};
	int16x8x2_t a;
	if constexpr (Channels == 1) {
		if constexpr (Depth == 1) {
			a = join(vld1q_u8(at - 1), vld1q_u8(at));
		}
		else if constexpr (Depth == 2) {
			const uint8x16x2_t v = vld2q_u8(at);
			a = join(v.val[0], v.val[1]);
		}
		else if constexpr (Depth == 3) {
			const uint8x16x3_t v = vld3q_u8(at);
			a = join(v.val[1], v.val[2]);
		}
		else {
			const uint8x16x4_t v = vld4q_u8(at);
			a = join(v.val[2], v.val[3]);
		}
		p.l0 = a.val[0];
		p.l1 = a.val[1];
	}
	else if constexpr (Depth == 1) {
		// Left's low byte is the byte before it (the frame before's right); right's is its frame's left
		const uint8x16x2_t v = vld2q_u8(at - 1);
		const uint8x16x2_t w = vld2q_u8(at);
		a = join(v.val[0], v.val[1]);
		const int16x8x2_t b = join(w.val[0], w.val[1]);
		p = {.l0 = a.val[0], .l1 = a.val[1], .r0 = b.val[0], .r1 = b.val[1]};
	}
	else if constexpr (Depth == 2) {
		const uint8x16x4_t v = vld4q_u8(at);
		a = join(v.val[0], v.val[1]);
		const int16x8x2_t b = join(v.val[2], v.val[3]);
		p = {.l0 = a.val[0], .l1 = a.val[1], .r0 = b.val[0], .r1 = b.val[1]};
	}
	else {
		// Eight frames: each sample's top two bytes, left and right alternating, then split
		if constexpr (Depth == 3) {
			const uint8x16x3_t v = vld3q_u8(at);
			a = join(v.val[1], v.val[2]);
		}
		else {
			const uint8x16x4_t v = vld4q_u8(at);
			a = join(v.val[2], v.val[3]);
		}
		const int16x8x2_t split = vuzpq_s16(a.val[0], a.val[1]);
		p.l0 = split.val[0];
		p.r0 = split.val[1];
	}
	return p;
}

/// Taps of @p n frames in memory order from @p s0 (the first frame's first sample byte), memory frame i going to slot
/// `Reverse ? n − 1 − i : i`: forward play stores reversed, so the newest frame lands lowest.
template <int Depth, int Channels, bool Reverse>
void ascend(const uint8_t* s0, int32_t n, int16_t* l, int16_t* r) {
	constexpr int32_t kFrameBytes = Depth * Channels;
	constexpr int32_t kStep = kPassFrames<Depth, Channels>;
	auto put = [&] [[gnu::always_inline]] (int16_t* to, int32_t i, int16x8_t v) {
		if constexpr (Reverse) {
			vst1q_s16(to + (n - i - 8), reversed(v));
		}
		else {
			vst1q_s16(to + i, v);
		}
	};
	int32_t i = 0;
	for (; i + kStep <= n; i += kStep) {
		const PassTaps p = pass<Depth, Channels>(s0 + (static_cast<std::ptrdiff_t>(i) * kFrameBytes));
		put(l, i, p.l0);
		if constexpr (kStep == 16) {
			put(l, i + 8, p.l1);
		}
		if constexpr (Channels == 2) {
			put(r, i, p.r0);
			if constexpr (kStep == 16) {
				put(r, i + 8, p.r1);
			}
		}
	}
	for (; i < n; i++) {
		const uint8_t* top = s0 + (static_cast<std::ptrdiff_t>(i) * kFrameBytes) + Depth - 2;
		const int32_t slot = Reverse ? n - 1 - i : i;
		l[slot] = halfword(top);
		if constexpr (Channels == 2) {
			r[slot] = halfword(top + Depth);
		}
	}
}

template <int Depth, int Channels>
void decode_as(const FrameRun& run, int32_t from, int32_t frames, int16_t* l, int16_t* r) {
	const auto* origin = reinterpret_cast<const uint8_t*>(run.first);
	if (run.stride > 0) {
		// Memory order is play order: the first frame decoded is the oldest
		const uint8_t* s0 = origin + (static_cast<std::ptrdiff_t>(from) * run.stride) + 4 - Depth;
		ascend<Depth, Channels, true>(s0, frames, l, r);
	}
	else {
		// Memory order is the reverse of play order: from the lowest frame up is newest first
		const int32_t lowest = from + frames - 1;
		const uint8_t* s0 = origin + (static_cast<std::ptrdiff_t>(lowest) * run.stride) + 4 - Depth;
		ascend<Depth, Channels, false>(s0, frames, l, r);
	}
}

template <int Channels>
void decode_channels(const FrameRun& run, int32_t from, int32_t frames, int16_t* l, int16_t* r) {
	switch (run.byte_depth) {
	case 1:
		decode_as<1, Channels>(run, from, frames, l, r);
		break;
	case 2:
		decode_as<2, Channels>(run, from, frames, l, r);
		break;
	case 3:
		decode_as<3, Channels>(run, from, frames, l, r);
		break;
	default:
		decode_as<4, Channels>(run, from, frames, l, r);
		break;
	}
}

} // namespace

void decode_taps(const FrameRun& run, int32_t from, int32_t frames, int16_t* l, int16_t* r) {
	if (frames <= 0) {
		return;
	}
	if (run.channels == 2) {
		decode_channels<2>(run, from, frames, l, r);
	}
	else {
		decode_channels<1>(run, from, frames, l, r);
	}
}

void load_history(Staging& s, const Interpolator& window, int32_t channels) {
	std::memcpy(s.l.data() + (kStageTaps - kHistoryTaps), window.window_l().data(), kHistoryTaps * sizeof(int16_t));
	if (channels == 2) {
		std::memcpy(s.r.data() + (kStageTaps - kHistoryTaps), window.window_r().data(), kHistoryTaps * sizeof(int16_t));
	}
}

void store_history(const Staging& s, int32_t newest, Interpolator& window, int32_t channels, int32_t slots) {
	auto feed = window.fill_head(static_cast<std::size_t>(slots));
	for (int32_t k = slots - 1; k >= 0; k--) {
		const auto at = static_cast<std::size_t>(newest + k);
		if (channels == 2) {
			feed.put(s.l[at], s.r[at]);
		}
		else {
			feed.put(s.l[at]);
		}
	}
}

} // namespace deluge::dsp::fixed::sample_read
