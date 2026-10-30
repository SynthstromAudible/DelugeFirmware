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

#include "reference_filters.h"
#include "wavetable_fixture.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

/// @file
/// @brief The int32 real FFT's workloads: deterministic inputs at every size the firmware transforms, the digest of a
/// transform's output, and the extent of its factor and twiddle tables.
namespace real_fft_workloads {

enum class Direction : uint8_t { Forward, Inverse };
enum class InputSet : uint8_t { Random, Impulses, FullScale, RealCycles };

inline constexpr int32_t kMinForwardMagnitude = 4; ///< The smallest forward transform: 16 points.
inline constexpr int32_t kMinInverseMagnitude = 3; ///< The smallest inverse transform: 8 points.
inline constexpr int32_t kMaxMagnitude = 13;       ///< The largest transform: 8192 points.

using filters_reference::Digest; // the one word digest the dsp specs share

/// @brief Digest of a transform's output words, in order: forward, 2^(m-1)+1 bins as (r, i) words; inverse, 2^m
/// samples.
[[nodiscard]] inline uint64_t output_digest(std::span<const int32_t> words) {
	Digest d;
	for (const int32_t w : words) {
		d.add(w);
	}
	return d.value;
}

namespace detail {

/// @brief A xorshift32 stream seeded per (magnitude, set).
struct Xorshift {
	uint32_t x;
	uint32_t next() {
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		return x;
	}
};

inline uint32_t seed(InputSet set, int32_t magnitude) {
	return 0x9E3779B9U ^ (static_cast<uint32_t>(magnitude) << 8) ^ static_cast<uint32_t>(set);
}

/// @brief The real-cycles fixture's samples: tests/integration/golden/presets/wavetable/static.wav.
inline const std::vector<int32_t>& real_cycles() {
	static const std::vector<int32_t> samples = [] {
		const auto f = wavetable_fixture::load(wavetable_fixture::repo_root()
		                                       / "tests/integration/golden/presets/wavetable/static.wav");
		assert(f.has_value() && f->samples.size() >= 8193);
		return f->samples;
	}();
	return samples;
}

} // namespace detail

/// @brief 2^magnitude real samples: random words with the loader's 12 bits of headroom, two opposite impulses, full
/// scale alternating, or a real cycle's first samples with the same headroom.
[[nodiscard]] inline std::vector<int32_t> forward_input(InputSet set, int32_t magnitude) {
	const auto n = std::size_t{1} << magnitude;
	std::vector<int32_t> v(n, 0);
	switch (set) {
	case InputSet::Random: {
		detail::Xorshift r{detail::seed(set, magnitude)};
		for (auto& s : v) {
			s = static_cast<int32_t>(r.next()) >> 12;
		}
		break;
	}
	case InputSet::Impulses:
		v[0] = 1 << 19;
		v[(n / 2) - 1] = -(1 << 19);
		break;
	case InputSet::FullScale:
		for (std::size_t i = 0; i < n; ++i) {
			v[i] = (i % 2) == 0 ? INT32_MAX : INT32_MIN;
		}
		break;
	case InputSet::RealCycles:
		for (std::size_t i = 0; i < n; ++i) {
			v[i] = detail::real_cycles()[i] >> 12;
		}
		break;
	}
	return v;
}

/// @brief 2^(magnitude-1)+1 bins as interleaved (r, i) words: random words with 8 bits of headroom, three real bins
/// (0, 1 and the Nyquist bin), every bin at full scale, or a real cycle's samples taken in pairs as (r, i).
[[nodiscard]] inline std::vector<int32_t> inverse_input(InputSet set, int32_t magnitude) {
	const auto bins = (std::size_t{1} << (magnitude - 1)) + 1;
	std::vector<int32_t> v(2 * bins, 0);
	switch (set) {
	case InputSet::Random: {
		detail::Xorshift r{detail::seed(set, magnitude)};
		for (auto& w : v) {
			w = static_cast<int32_t>(r.next()) >> 8;
		}
		break;
	}
	case InputSet::Impulses:
		v[0] = 1 << 24;
		v[2] = 1 << 24;
		v[2 * (bins - 1)] = -(1 << 24);
		break;
	case InputSet::FullScale:
		for (std::size_t k = 0; k < bins; ++k) {
			v[2 * k] = INT32_MAX;
			v[(2 * k) + 1] = INT32_MIN;
		}
		break;
	case InputSet::RealCycles:
		for (std::size_t k = 0; k < bins; ++k) {
			v[2 * k] = detail::real_cycles()[2 * k] >> 8;
			v[(2 * k) + 1] = detail::real_cycles()[(2 * k) + 1] >> 8;
		}
		break;
	}
	return v;
}

/// @brief How many stage twiddles the factor list's stages after the first fill; entries past that count are never
/// written and never read. Each later stage s holds (radix − 1) twiddles per remaining point.
[[nodiscard]] inline int32_t written_stage_twiddles(std::span<const int32_t> factors) {
	const int32_t stages = factors[0];
	int32_t count = 0;
	for (int32_t s = stages - 1; s >= 1; --s) {
		count += factors[(2 * s) + 1] * (factors[2 * s] - 1);
	}
	return count;
}

/// @brief The factor list's written words: stage count, first stride, (radix, remaining) per stage, algorithm flag.
[[nodiscard]] inline int32_t written_factor_words(std::span<const int32_t> factors) {
	return (2 * (factors[0] + 1)) + 1;
}

/// @brief Digest of the factors (their written words), the stage twiddles (their written entries) and the super
/// twiddles (ncfft / 2 entries), each twiddle as (r, i) words.
[[nodiscard]] inline uint64_t twiddle_digest(std::span<const int32_t> factors, std::span<const int32_t> stage_ri,
                                             std::span<const int32_t> super_ri) {
	Digest d;
	for (int32_t i = 0; i < written_factor_words(factors); ++i) {
		d.add(factors[static_cast<std::size_t>(i)]);
	}
	for (const int32_t w : stage_ri.first(static_cast<std::size_t>(2 * written_stage_twiddles(factors)))) {
		d.add(w);
	}
	for (const int32_t w : super_ri) {
		d.add(w);
	}
	return d.value;
}

} // namespace real_fft_workloads
