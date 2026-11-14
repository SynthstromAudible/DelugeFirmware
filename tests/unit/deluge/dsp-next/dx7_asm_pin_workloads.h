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

/// @file
/// @brief The cases the DX7 asm pin runs the device's NEON FM kernel over: what FmCore::render hands it (input or
/// zeros, mix or zeros, output, simd_n(n), phase0, freq, gain1, dgain) across every padding form, gain ramp and
/// phase run.

#pragma once

#include "dsp-next/fixed/noise/noise.h"
#include "reference_dx7.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#if defined(__arm__)
extern "C" void reference_neon_fm_kernel(const int32_t* in, const int32_t* busin, int32_t* out, int count,
                                         int32_t phase0, int32_t freq, int32_t gain1, int32_t dgain);
#endif

namespace dx7_asm_pin {

struct PinCase {
	const char* name;
	bool modulated;
	bool add;
	int n;
	int32_t phase0;
	int32_t freq;
	int32_t gain_out;
	int32_t level_in;
	uint32_t seed;
};

inline constexpr std::array<int, 15> kLengths{1, 4, 5, 11, 12, 13, 16, 24, 28, 31, 64, 100, 124, 127, 128};

struct GainSet {
	const char* name;
	int32_t gain_out;
	int32_t level_in;
};

struct PhaseSet {
	const char* name;
	int32_t phase0;
	int32_t freq;
};

inline constexpr std::array<PhaseSet, 4> kPhases{{
    {"slow", 0, 0x00123456},
    {"wraps", 0x7fff0000, 0x01000000},
    {"bit23", 0x00ffffff, 0x7fffffff},
    {"backwards", 0, static_cast<int32_t>(0xfff00000)},
}};

inline constexpr std::size_t kCaseCount = 2 * 2 * kLengths.size() * 5 * kPhases.size();

/// @return fm_core.cpp's simd_n with neon set: n rounded up to a multiple of 4, and to 12k + 4 where it would be
/// 12k + 8.
[[nodiscard]] inline int simd_n(int n) {
	int nmod = 1 + (n + 11) % 12;
	return nmod == 8 ? n + 4 : (n + 3) & ~3;
}

/// @return How many samples the asm writes for a count of simd_n(n): it renders blocks of 12 (at least one) after
/// its first 4, and a last block of 4 only when the count is exactly 12k + 4, so other counts write up to the next
/// whole block of 12.
[[nodiscard]] inline int asm_writes(int n) {
	const int count = simd_n(n);
	const int after_first = count - 4;
	if (after_first > 0 && after_first % 12 == 0) {
		return count;
	}
	const int blocks = std::max(1, (after_first + 11) / 12);
	return 12 * blocks;
}

struct Gains {
	int32_t gain1, gain2, dgain;
};

/// @return fm_core.cpp's gains for an op: gain1 the last gain_out, gain2 from the level, and the per-sample step.
DELUGE_REFERENCE_DX7_LEGACY_ARITHMETIC inline Gains modern_gains(const dx7_reference::Tables& t, int32_t gain_out,
                                                                 int32_t level_in, int n) {
	const int inv_n = (1 << 30) / n;
	const int32_t gain1 = gain_out;
	const int32_t gain2 = dx7_reference::exp2_lookup(t, level_in - (14 * (1 << 24)));
	const int32_t dgain = (int)((((int64_t)(gain2 - gain1 + (n >> 1))) * (int64_t)(inv_n)) >> 30);
	return {gain1, gain2, dgain};
}

/// @return The five gain sets: rising from silence, falling to it, held, loud, and a zero step at gain 1.
inline const std::array<GainSet, 5>& gain_sets() {
	static const std::array<GainSet, 5> sets = [] {
		static dx7_reference::Tables t{};
		dx7_reference::init_tables(t);
		const int32_t held = modern_gains(t, 0, 15 << 24, 1).gain2;
		const int32_t one = modern_gains(t, 0, 14 << 24, 1).gain2;
		return std::array<GainSet, 5>{{
		    {"rising", 0, 16 << 24},
		    {"falling", 0x07000000, 0},
		    {"held", held, 15 << 24},
		    {"loud", 0, 0x10f00000},
		    {"unity", one, 14 << 24},
		}};
	}();
	return sets;
}

/// @return Every case, in a fixed order; a case's seed is its index.
inline std::span<const PinCase> all_cases() {
	static const std::vector<PinCase> cases = [] {
		static std::vector<std::string> names;
		names.reserve(kCaseCount);
		std::vector<PinCase> c;
		c.reserve(kCaseCount);
		for (const bool modulated : {false, true}) {
			for (const bool add : {false, true}) {
				for (const int n : kLengths) {
					for (const GainSet& g : gain_sets()) {
						for (const PhaseSet& p : kPhases) {
							names.push_back(std::string(modulated ? "mod" : "pure") + (add ? "_add" : "_set") + "_n"
							                + std::to_string(n) + "_" + g.name + "_" + p.name);
							c.push_back({names.back().c_str(), modulated, add, n, p.phase0, p.freq, g.gain_out,
							             g.level_in, static_cast<uint32_t>(c.size())});
						}
					}
				}
			}
		}
		return c;
	}();
	return cases;
}

struct Inputs {
	alignas(16) int32_t modulation[144];  // NOLINT(*-avoid-c-arrays): the asm reads 16-byte-aligned blocks
	alignas(16) int32_t accumulator[144]; // NOLINT(*-avoid-c-arrays): as modulation
};

/// @brief Fills @p inputs from the case's seed: the modulation (zeros for a pure case) and the accumulator.
inline void fill_inputs(const PinCase& pin, Inputs& inputs) {
	deluge::dsp::fixed::SeededNoise noise{pin.seed + 1U};
	for (int32_t& m : inputs.modulation) {
		m = pin.modulated ? (noise.render() >> 4) : 0;
	}
	for (int32_t& a : inputs.accumulator) {
		a = noise.render() >> 3;
	}
}

/// @return A digest of @p out's first @p n words.
inline uint64_t digest_first_n(std::span<const int32_t> out, int n) {
	dx7_reference::Digest d;
	for (int i = 0; i < n; ++i) {
		d.add(out[static_cast<std::size_t>(i)]);
	}
	return d.value;
}

} // namespace dx7_asm_pin
