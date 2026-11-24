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
#include "dsp/fixed/sample_read/mixed_in.h"
#include "dsp/fixed/timestretch/crossfade.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace deluge::synth {

/// @file
/// @brief How the time stretcher's heads are mixed: the voice amplitude's set-up (X4), when the older head stops (X3)
/// and the copy of a stretched block into a cache and the output (W1) of docs/dev/time_stretcher_contracts.md.
///
/// The voice's amplitude ramps as a word, stepped by whole multiples of its step, and the cache stores each sample's
/// bytes, so the arithmetic here is word arithmetic on the typed values' raw words: every crossing below is one of
/// those words.

/// @brief The voice amplitude and its per-frame step, shifted up for the stretcher's mix.
struct StretchAmplitude {
	dsp::Q<31> amplitude;
	dsp::Q<31> step;
};

/// @brief X4's set-up: up 3, 1 more when pitched, 1 more when stretching, both words wrapping.
[[nodiscard]] constexpr StretchAmplitude stretch_amplitude(dsp::Q<31> amplitude, dsp::Q<31> step, bool pitched,
                                                           bool stretching) {
	int32_t shift = 3 + (pitched ? 1 : 0) + (stretching ? 1 : 0);
	auto up = [shift](dsp::Q<31> q) {
		return dsp::Q<31>::from_raw(static_cast<int32_t>(static_cast<uint32_t>(q.raw()) << shift));
	};
	return {.amplitude = up(amplitude), .step = up(step)};
}

/// @brief The level both heads read at and its step.
struct HeadLevel {
	dsp::Q<31> level;
	dsp::Q<31> step;
};

/// @brief X4's level: half the amplitude and step, or full scale and still while a cache is written (the copy into
/// the cache applies the amplitude).
[[nodiscard]] constexpr HeadLevel segment_level(StretchAmplitude a, bool writing_cache) {
	if (writing_cache) {
		return {.level = dsp::Q<31>::from_raw(INT32_MAX), .step = dsp::Q<31>::from_raw(0)};
	}
	return {.level = dsp::Q<31>::from_raw(a.amplitude.raw() >> 1), .step = dsp::Q<31>::from_raw(a.step.raw() >> 1)};
}

/// @brief After a segment of @p frames: the amplitude steps on, unless a cache is written (its copy stepped it).
constexpr void step_amplitude(StretchAmplitude& a, int32_t frames, bool writing_cache) {
	if (!writing_cache) {
		a.amplitude = dsp::Q<31>::from_raw(static_cast<int32_t>(static_cast<uint32_t>(a.amplitude.raw())
		                                                        + static_cast<uint32_t>(a.step.raw() * frames)));
	}
}

/// @brief Whether the older head still sounds (X3): its crossfade has not reached 1.0.
[[nodiscard]] constexpr bool older_reads_on(const dsp::fixed::timestretch::CrossfadeRamp& ramp) {
	return ramp.progress < (1u << 24);
}

/// @brief W1: a stretched block @p mix (@p channels words a frame) copied into the cache and mixed into @p out
/// (@p out_channels a frame), as many frames as @p out holds. Per frame the amplitude steps first; the cache takes the
/// top three bytes of each word doubled, left then right; a mono output takes `(l >> 1) + (r >> 1)`.
/// @param cache The cache bytes to write, 3 per channel per frame.
inline void write_cache_frames(std::span<const dsp::Q<31>> mix, int32_t channels, std::span<std::byte> cache,
                               std::span<dsp::Q<31>> out, int32_t out_channels, dsp::Q<31>& amplitude,
                               dsp::Q<31> step) {
	const auto frames = static_cast<int32_t>(out.size()) / out_channels;
	auto cache_word = [&cache](size_t at, int32_t word) {
		const auto doubled = static_cast<uint32_t>(word) << 1;
		cache[at] = static_cast<std::byte>(doubled >> 8);
		cache[at + 1] = static_cast<std::byte>(doubled >> 16);
		cache[at + 2] = static_cast<std::byte>(doubled >> 24);
	};
	for (int32_t f = 0; f < frames; f++) {
		amplitude = dsp::Q<31>::from_raw(
		    static_cast<int32_t>(static_cast<uint32_t>(amplitude.raw()) + static_cast<uint32_t>(step.raw())));
		const auto at = static_cast<size_t>(f * channels);
		int32_t l = mix[at].raw();
		cache_word(at * 3, l);
		int32_t r = 0;
		if (channels == 2) {
			r = mix[at + 1].raw();
			if (out_channels == 1) {
				l = (l >> 1) + (r >> 1);
			}
			cache_word(at * 3 + 3, r);
		}
		const auto o = static_cast<size_t>(f * out_channels);
		out[o] = dsp::fixed::sample_read::mixed_in(out[o], dsp::Q<31>::from_raw(l), amplitude);
		if (out_channels == 2) {
			out[o + 1] = dsp::fixed::sample_read::mixed_in(out[o + 1], dsp::Q<31>::from_raw(r), amplitude);
		}
	}
}

} // namespace deluge::synth
