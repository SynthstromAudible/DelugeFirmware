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

// The dry/wet blend. dry_wet_blend is pinned on hand-computed values at full scale and the extremes, then against the
// the two expressions it serves, copied here as their callers write them: the legacy fade's per-sample arithmetic and
// the compressor's, with its dry level ONE_Q31 − blend. Both run full-scale noise after the extremes, at fixed levels
// across the callers' range [0, ONE_Q31]. The blend at ONE_Q31 is not the wet signal, so the kernel's FULL_WET mode,
// which returns wet as the compressor's skip does, is pinned to differ from the blend there. Then Mono<DryWetBlend>
// against the blend, or the wet sample at full wet, across a configure that moves it into and out of FULL_WET.

#include "dsp-next/fixed/mix/dry_wet.h"

#include "dsp-next/core/concepts.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/fixed/noise/noise.h"
#include "util/fixedpoint.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

using namespace deluge::dsp;
using namespace deluge::dsp::fixed;

namespace {

using Mode = DryWetBlend::Mode;

static_assert(mixer_kernel<DryWetBlend>);
static_assert(moded_kernel<DryWetBlend>);
static_assert(!std::is_polymorphic_v<DryWetBlend>);
static_assert(mixer<Mono<DryWetBlend>>);
static_assert(DryWetBlend::coefficients({.level = ONE_Q31}).mode == Mode::FULL_WET);
static_assert(DryWetBlend::coefficients({.level = ONE_Q31}).level == ONE_Q31);
static_assert(DryWetBlend::coefficients({.level = ONE_Q31 - 1}).mode == Mode::BLEND);
static_assert(DryWetBlend::coefficients({.level = 1 << 30}).level == 1 << 30);
static_assert(DryWetBlend::coefficients({}).mode == Mode::BLEND);
static_assert(DryWetBlend::Coefficients{}.mode == Mode::BLEND);

/// The extremes of q31.
constexpr q31 kMin = std::numeric_limits<q31>::min();
constexpr q31 kMax = std::numeric_limits<q31>::max();
/// The levels the comparisons run, Q31, across the callers' range: fully dry, the least wet, a quarter, a half,
/// 1/√2, the most wet short of full, and full.
constexpr std::array<q31, 7> kLevels{0, 1, 1 << 29, 1 << 30, 0x5A82'7999, ONE_Q31 - 1, ONE_Q31};
/// The samples the long comparisons run.
constexpr std::size_t kLongRun = 10'000;
/// The seeds of the dry and wet noise.
constexpr uint32_t kDrySeed = 0xD1E7'0001;
constexpr uint32_t kWetSeed = 0x0E70'0002;

/// Every pairing of the extremes, zero and ±1, then @p n samples of full-scale seeded noise.
std::vector<q31> extremes_then_full_scale(std::size_t n, uint32_t seed, bool second) {
	constexpr std::array<q31, 6> kEdges{kMin, kMin + 1, -1, 0, 1, kMax};
	std::vector<q31> v;
	for (const q31 a : kEdges) {
		for (const q31 b : kEdges) {
			v.push_back(second ? b : a);
		}
	}
	SeededNoise noise{seed};
	for (std::size_t i = 0; i < n; ++i) {
		v.push_back(noise.render());
	}
	return v;
}

/// The legacy fade's blend arithmetic for one sample, written out without dry_wet_blend:
///     const q31 wet = multiply_32x32_rshift32(block[i], wet_);
///     block[i] = multiply_accumulate_32x32_rshift32_rounded(wet, scratch[i], ONE_Q31 - wet_) << 1;
q31 fade_blend(q31 filtered, q31 dry_copy, q31 wet_level) {
	const q31 wet = multiply_32x32_rshift32(filtered, wet_level);
	return multiply_accumulate_32x32_rshift32_rounded(wet, dry_copy, ONE_Q31 - wet_level) << 1;
}

/// The compressor's blend for one channel, as rms_feedback_compressor.cpp writes it, with setBlend's levels:
///     dry_ = ONE_Q31 - blend;
///     wet_ = blend;
///     ...
///     sample.l = multiply_32x32_rshift32(sample.l, wet_);
///     sample.l = multiply_accumulate_32x32_rshift32_rounded(sample.l, scratch[i].l, dry_);
///     sample.l <<= 1;
q31 compressor_blend(q31 sample, q31 scratch, q31 blend) {
	const q31 dry_level = ONE_Q31 - blend;
	const q31 wet_level = blend;
	sample = multiply_32x32_rshift32(sample, wet_level);
	sample = multiply_accumulate_32x32_rshift32_rounded(sample, scratch, dry_level);
	sample <<= 1;
	return sample;
}

/// What a block at @p level mixes: the wet sample unchanged at exactly ONE_Q31, as the compressor skips its blend, else
/// the blend.
q31 expected_mix(q31 level, q31 dry, q31 wet) {
	return level == ONE_Q31 ? wet : dry_wet_blend(dry, wet, level);
}

/// The block lengths the wrapper comparison runs, ending on a long odd one.
constexpr std::array<std::size_t, 6> kLengths{1, 2, 3, 7, 64, 129};

} // namespace

// clang-format off
describe dry_wet_spec("DryWetBlend", ${
	context("dry_wet_blend", _ {
		it("blends full-scale and extreme samples to hand-computed values", _ {
			// Full scale times full scale keeps the high word 2^30 - 1, doubled to 2^31 - 2.
			expect(dry_wet_blend(0, kMax, ONE_Q31)).to_equal(q31{0x7FFF'FFFE});
			expect(dry_wet_blend(kMax, 0, 0)).to_equal(q31{0x7FFF'FFFE});
			expect(dry_wet_blend(kMax, kMax, 1 << 30)).to_equal(q31{0x7FFF'FFFC});
			expect(dry_wet_blend(kMin, kMin, 1 << 30)).to_equal(kMin + 2);
			expect(dry_wet_blend(kMax, kMin, ONE_Q31)).to_equal(kMin);
			expect(dry_wet_blend(1, 1, ONE_Q31)).to_equal(q31{0});
			expect(dry_wet_blend(12345, -7, 1 << 30)).to_equal(q31{6168});
		});

		it("wraps the dry level and the doubling as two's complement at a negative level", _ {
			// ONE_Q31 - INT32_MIN wraps to -1; the doubled 2^30 wraps to INT32_MIN.
			expect(dry_wet_blend(0, kMin, kMin)).to_equal(kMin);
			expect(dry_wet_blend(kMax, kMin, kMin)).to_equal(kMin);
		});

		it("blends exactly as the legacy fade's arithmetic, at every level", _ {
			const std::vector<q31> dry = extremes_then_full_scale(kLongRun, kDrySeed, false);
			const std::vector<q31> wet = extremes_then_full_scale(kLongRun, kWetSeed, true);
			for (const q31 level : kLevels) {
				for (std::size_t i = 0; i < dry.size(); ++i) {
					expect(dry_wet_blend(dry[i], wet[i], level)).to_equal(fade_blend(wet[i], dry[i], level));
				}
			}
		});

		it("blends exactly as the compressor's arithmetic, at every level", _ {
			const std::vector<q31> dry = extremes_then_full_scale(kLongRun, kDrySeed, false);
			const std::vector<q31> wet = extremes_then_full_scale(kLongRun, kWetSeed, true);
			for (const q31 level : kLevels) {
				for (std::size_t i = 0; i < dry.size(); ++i) {
					expect(dry_wet_blend(dry[i], wet[i], level)).to_equal(compressor_blend(wet[i], dry[i], level));
				}
			}
		});

		it("is not the wet signal at full wet: a negative sample loses its lowest bit, a positive one lands 1 or 2 below "
		   "itself with its lowest bit clear",
		   _ {
			const std::vector<q31> dry = extremes_then_full_scale(kLongRun, kDrySeed, false);
			const std::vector<q31> wet = extremes_then_full_scale(kLongRun, kWetSeed, true);
			std::size_t differing = 0;
			for (std::size_t i = 0; i < dry.size(); ++i) {
				differing += dry_wet_blend(dry[i], wet[i], ONE_Q31) != wet[i] ? 1 : 0;
			}
			expect(differing).to_be_greater_than(std::size_t{0});
			expect(dry_wet_blend(0, kMax, ONE_Q31)).to_equal(kMax - 1);
			expect(dry_wet_blend(0, 2, ONE_Q31)).to_equal(q31{0});
			expect(dry_wet_blend(0, -3, ONE_Q31)).to_equal(q31{-4});
			expect(dry_wet_blend(0, -2, ONE_Q31)).to_equal(q31{-2});
		});
	});

	context("the kernel", _ {
		it("chooses FULL_WET at exactly ONE_Q31 and BLEND at every other level", _ {
			expect(DryWetBlend::coefficients({.level = ONE_Q31}).mode).to_equal(Mode::FULL_WET);
			for (const q31 level : {kMin, -1, q31{0}, q31{1}, q31{1 << 30}, ONE_Q31 - 1}) {
				const DryWetBlend::Coefficients c = DryWetBlend::coefficients({.level = level});
				expect(c.mode).to_equal(Mode::BLEND);
				expect(c.level).to_equal(level);
			}
		});

		it("blends as dry_wet_blend in BLEND, at every level, full included", _ {
			const std::vector<q31> dry = extremes_then_full_scale(kLongRun, kDrySeed, false);
			const std::vector<q31> wet = extremes_then_full_scale(kLongRun, kWetSeed, true);
			for (const q31 level : kLevels) {
				const DryWetBlend::Coefficients c{.level = level, .mode = Mode::BLEND};
				DryWetBlend::State s{};
				for (std::size_t i = 0; i < dry.size(); ++i) {
					expect(DryWetBlend::step<Mode::BLEND>(c, {}, s, dry[i], wet[i]))
					    .to_equal(dry_wet_blend(dry[i], wet[i], level));
				}
			}
		});

		it("returns the wet sample unchanged in FULL_WET, as the compressor's skip does", _ {
			const std::vector<q31> dry = extremes_then_full_scale(kLongRun, kDrySeed, false);
			const std::vector<q31> wet = extremes_then_full_scale(kLongRun, kWetSeed, true);
			const DryWetBlend::Coefficients c = DryWetBlend::coefficients({.level = ONE_Q31});
			DryWetBlend::State s{};
			for (std::size_t i = 0; i < dry.size(); ++i) {
				expect(DryWetBlend::step<Mode::FULL_WET>(c, {}, s, dry[i], wet[i])).to_equal(wet[i]);
			}
			expect(DryWetBlend::step<Mode::FULL_WET>(c, {}, s, 0, kMax)).to_equal(kMax);
			expect(DryWetBlend::step<Mode::FULL_WET>(c, {}, s, 1, 1)).to_equal(q31{1});
		});
	});

	context("Mono<DryWetBlend>", _ {
		it("mixes as the blend, or passes the wet sample at full wet, across a configure into FULL_WET and back", _ {
			const std::vector<q31> dry = extremes_then_full_scale(kLongRun, kDrySeed, false);
			const std::vector<q31> wet = extremes_then_full_scale(kLongRun, kWetSeed, true);
			constexpr std::array<q31, 4> kBlockLevels{1 << 30, ONE_Q31, 0x5A82'7999, ONE_Q31};
			Mono<DryWetBlend> mono;
			std::size_t start = 0;
			std::size_t full_wet_blocks = 0;
			for (const q31 level : kBlockLevels) {
				for (const std::size_t n : kLengths) {
					mono.configure({.level = level});
					full_wet_blocks += mono.coefficients().mode == Mode::FULL_WET ? 1 : 0;
					std::vector<q31> out(n);
					mono.renderBlock(fixed::Signal{dry.data() + start, n}, fixed::Signal{wet.data() + start, n}, out);
					for (std::size_t i = 0; i < n; ++i) {
						expect(out[i]).to_equal(expected_mix(level, dry[start + i], wet[start + i]));
					}
					start += n;
				}
			}
			expect(full_wet_blocks).to_equal(2 * kLengths.size());
		});

		it("mixes a single pair as the kernel does", _ {
			Mono<DryWetBlend> mono{{.level = 1 << 30}};
			expect(mono.render(kMax, kMax)).to_equal(q31{0x7FFF'FFFC});
			mono.configure({.level = ONE_Q31});
			expect(mono.render(0, kMax)).to_equal(kMax);
		});
	});
});
// clang-format on

CPPSPEC_MAIN(dry_wet_spec);
