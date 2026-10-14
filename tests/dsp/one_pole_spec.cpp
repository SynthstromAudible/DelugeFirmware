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

// The one-pole section. The legacy one-pole class, as the reference (reference_filters.h) holds it, is pinned on 16
// samples of a ramp at moveability 0x20000000. The OnePole kernel is pinned against the class, sample by sample from
// equal memories: LOW_PASS against lowPass, ALL_PASS against allPass, HIGH_PASS against the input less lowPass, as the
// high-pass ladder and the compressor write it, and the feedback reads against the class's. Full-scale and extreme
// inputs, whose arithmetic overflows q31 in every mode's own operations, and the feedback reads at the memories they
// leave and at the extremes, are pinned against the class's expressions computed through uint32_t, wrapping as the
// hardware does. Then Mono<OnePole> against a hand loop over the kernel, across a mode switch.

#include "dsp-next/fixed/filter/one_pole.h"

#include "dsp-next/core/concepts.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/fixed/noise/noise.h"
#include "reference_filters.h"
#include "util/fixedpoint.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

using namespace deluge::dsp;
using namespace deluge::dsp::fixed;

namespace {

using Mode = OnePole::Mode;

static_assert(processor_kernel<OnePole>);
static_assert(moded_kernel<OnePole>);
static_assert(!std::is_polymorphic_v<OnePole>);
static_assert(processor<Mono<OnePole>>);
static_assert(OnePole::coefficients({.moveability = 1 << 29, .mode = Mode::ALL_PASS}).m == 1 << 29);
static_assert(OnePole::coefficients({.moveability = 1 << 29, .mode = Mode::ALL_PASS}).mode == Mode::ALL_PASS);
static_assert(OnePole::coefficients({}).mode == Mode::LOW_PASS);
static_assert(OnePole::State{}.z == 0);

/// The legacy pins' moveability.
constexpr int32_t kRampMoveability = 0x20000000;

/// A ramp from -2^30 rising by 2^27 per sample.
constexpr int32_t ramp(int32_t i) {
	return -(1 << 30) + (i * (1 << 27));
}

/// The moveabilities the comparisons run, Q31: the granular kernel's, one mid, and one near 2^31.
constexpr std::array<q31, 3> kMoveabilities{1 << 29, 1 << 30, 0x7800'0000};
/// The samples the long comparisons run.
constexpr std::size_t kLongRun = 10'000;
/// The seed of every noise input.
constexpr uint32_t kSeed = 0x0E90'1E5D;
/// The extremes of q31.
constexpr q31 kMin = std::numeric_limits<q31>::min();
constexpr q31 kMax = std::numeric_limits<q31>::max();
/// The feedback amounts the feedback reads run, Q31: the extremes, silence, and three between.
constexpr std::array<q31, 6> kAmounts{kMin, -(1 << 29), 0, 1 << 29, 0x5A82'7999, kMax};

/// The shift that quietens the comparisons' noise, so that none of the class's signed operations can overflow.
/// @details With μ the moveability as a fraction (m / 2^31), each step sets z ← (1 − 2μ)z + 2μx, so a bounded input
/// bounds the memory at |z| ≤ μ/(1 − μ) · max|x| when μ > 1/2. At the comparisons' highest moveability, 0x78000000
/// (μ = 15/16), that gain is 15: at an input of 2^-5 of full scale, |z| ≤ 15/32, x − z stays within 1/2 and 2y − x
/// within 31/32 of full scale, give or take the product's rounding. 2^-5 is the largest power of two with that
/// guarantee: at 2^-4, 2y − x could reach 31/16.
constexpr int kQuietShift = 5;

/// @p n samples of seeded noise at 2^-kQuietShift of full scale.
std::vector<q31> quiet_noise(std::size_t n, uint32_t seed) {
	SeededNoise noise{seed};
	std::vector<q31> v(n);
	for (q31& x : v) {
		x = noise.render() >> kQuietShift;
	}
	return v;
}

/// 64 samples alternating between INT32_MIN and INT32_MAX, then @p n samples of full-scale seeded noise.
std::vector<q31> extreme_then_full_scale(std::size_t n, uint32_t seed) {
	std::vector<q31> v;
	for (std::size_t i = 0; i < 64; ++i) {
		v.push_back(i % 2 == 0 ? kMin : kMax);
	}
	SeededNoise noise{seed};
	for (std::size_t i = 0; i < n; ++i) {
		v.push_back(noise.render());
	}
	return v;
}

/// @p a - @p b through uint32_t: two's complement, wrapping modulo 2^32.
q31 sub_mod(q31 a, q31 b) {
	return static_cast<q31>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}

/// @p a + @p b through uint32_t: two's complement, wrapping modulo 2^32.
q31 add_mod(q31 a, q31 b) {
	return static_cast<q31>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

/// Whether @p a - @p b leaves q31.
bool difference_overflows(q31 a, q31 b) {
	const int64_t d = int64_t{a} - int64_t{b};
	return d < int64_t{kMin} || d > int64_t{kMax};
}

/// Whether @p a + @p b leaves q31.
bool sum_overflows(q31 a, q31 b) {
	const int64_t t = int64_t{a} + int64_t{b};
	return t < int64_t{kMin} || t > int64_t{kMax};
}

/// The class's feedbackOutput on @p memory, its shift written through uint32_t so that it wraps as the class's does.
q31 wrapped_feedback(q31 memory, q31 amount) {
	return static_cast<q31>(static_cast<uint32_t>(multiply_32x32_rshift32_rounded(memory, amount)) << 2);
}

/// Whether the class's feedbackOutput on @p memory leaves q31 in its shift.
bool feedback_overflows(q31 memory, q31 amount) {
	const int64_t shifted = int64_t{multiply_32x32_rshift32_rounded(memory, amount)} * 4;
	return shifted < int64_t{kMin} || shifted > int64_t{kMax};
}

/// The overflows the class's signed form would have met, counted per operation.
struct Wraps {
	std::size_t input_less_memory = 0;  ///< x - z, in every mode.
	std::size_t step_plus_memory = 0;   ///< v + z, in every mode.
	std::size_t output_plus_step = 0;   ///< y + v, in every mode.
	std::size_t input_less_output = 0;  ///< HIGH_PASS's x - y.
	std::size_t doubled_output = 0;     ///< ALL_PASS's y * 2.
	std::size_t doubled_less_input = 0; ///< ALL_PASS's 2y - x, from the wrapped 2y.
};

/// The class's lowPass and allPass, and the high-pass its callers write, on one memory, written through uint32_t so
/// that they wrap as the hardware does.
struct WrappingReference {
	q31 memory = 0;
	Wraps wraps{};

	/// One sample: the low-pass output, with the memory advanced.
	q31 low_pass(q31 input, q31 moveability) {
		wraps.input_less_memory += difference_overflows(input, memory) ? 1 : 0;
		const q31 step = multiply_32x32_rshift32_rounded(sub_mod(input, memory), moveability) << 1;
		wraps.step_plus_memory += sum_overflows(step, memory) ? 1 : 0;
		const q31 output = add_mod(step, memory);
		wraps.output_plus_step += sum_overflows(output, step) ? 1 : 0;
		memory = add_mod(output, step);
		return output;
	}

	/// One sample: the input less the low-pass output.
	q31 high_pass(q31 input, q31 moveability) {
		const q31 output = low_pass(input, moveability);
		wraps.input_less_output += difference_overflows(input, output) ? 1 : 0;
		return sub_mod(input, output);
	}

	/// One sample: twice the low-pass output less the input.
	q31 all_pass(q31 input, q31 moveability) {
		const q31 output = low_pass(input, moveability);
		wraps.doubled_output += sum_overflows(output, output) ? 1 : 0;
		const q31 doubled = add_mod(output, output);
		wraps.doubled_less_input += difference_overflows(doubled, input) ? 1 : 0;
		return sub_mod(doubled, input);
	}
};

/// One kernel step in @p mode, dispatched here as the wrapper does once per block.
q31 step_in(Mode mode, const OnePole::Coefficients& c, OnePole::State& s, q31 x) {
	switch (mode) {
	case Mode::LOW_PASS:
		return OnePole::step<Mode::LOW_PASS>(c, {}, s, x);
	case Mode::HIGH_PASS:
		return OnePole::step<Mode::HIGH_PASS>(c, {}, s, x);
	case Mode::ALL_PASS:
		return OnePole::step<Mode::ALL_PASS>(c, {}, s, x);
	}
	return 0;
}

/// The block lengths the wrapper comparisons run, ending on a long odd one.
constexpr std::array<std::size_t, 6> kLengths{1, 2, 3, 7, 64, 129};
/// Every mode, in the order the mode-switch comparison visits them.
constexpr std::array<Mode, 3> kModes{Mode::LOW_PASS, Mode::HIGH_PASS, Mode::ALL_PASS};

} // namespace

// clang-format off
describe one_pole_spec("OnePole", ${
	context("the legacy one-pole, copied", _ {
		it("low-passes a ramp exactly", _ {
			constexpr std::array<int32_t, 16> expected{
			    -268435456, -637534208, -754974720, -746586112, -675282944, -572522496, -454033408, -327680000,
			    -197394432, -65142784, 68091904, 201818112, 335790080, 469884928, 604041216, 738228224};
			filters_reference::ReferenceOnePole pole;
			for (int32_t i = 0; i < 16; i++) {
				expect(pole.lowPass(ramp(i), kRampMoveability)).to_equal(expected[i]);
			}
			expect(pole.memory()).to_equal(805326848);
			expect(pole.feedbackOutput(kRampMoveability)).to_equal(402663424);
			expect(pole.feedbackOutputUnshifted(kRampMoveability)).to_equal(100665856);
		});
		it("all-passes a ramp exactly", _ {
			constexpr std::array<int32_t, 16> expected{
			    536870912, -335544320, -704643072, -822083584, -813694976, -742391808, -639631360, -521142272,
			    -394788864, -264503296, -132251648, 983040, 134709248, 268681216, 402776064, 536932352};
			filters_reference::ReferenceOnePole pole;
			for (int32_t i = 0; i < 16; i++) {
				expect(pole.allPass(ramp(i), kRampMoveability)).to_equal(expected[i]);
			}
			expect(pole.memory()).to_equal(805326848);
		});
		it("starts silent and resets to silence", _ {
			filters_reference::ReferenceOnePole pole;
			expect(pole.memory()).to_equal(0);
			std::ignore = pole.lowPass(ramp(3), kRampMoveability);
			pole.reset();
			expect(pole.memory()).to_equal(0);
		});
	});

	context("the kernel", _ {
		it("low-passes exactly as the class's lowPass", _ {
			for (const q31 m : kMoveabilities) {
				const std::vector<q31> in = quiet_noise(kLongRun, kSeed);
				const OnePole::Coefficients c = OnePole::coefficients({.moveability = m, .mode = Mode::LOW_PASS});
				OnePole::State s{};
				filters_reference::ReferenceOnePole pole;
				for (const q31 x : in) {
					expect(OnePole::step<Mode::LOW_PASS>(c, {}, s, x)).to_equal(pole.lowPass(x, m));
				}
				expect(s.z).to_equal(pole.memory());
				expect(s.z).not_().to_equal(q31{0});
			}
		});

		it("high-passes exactly as the high-pass ladder and the compressor subtract the class's lowPass", _ {
			for (const q31 m : kMoveabilities) {
				const std::vector<q31> in = quiet_noise(kLongRun, kSeed);
				const OnePole::Coefficients c = OnePole::coefficients({.moveability = m, .mode = Mode::HIGH_PASS});
				OnePole::State s{};
				filters_reference::ReferenceOnePole pole;
				for (const q31 input : in) {
					// The legacy high-pass ladder and compressor detector: the input less the section's lowPass.
					const q31 first_output = input - pole.lowPass(input, m);
					expect(OnePole::step<Mode::HIGH_PASS>(c, {}, s, input)).to_equal(first_output);
				}
				expect(s.z).to_equal(pole.memory());
				expect(s.z).not_().to_equal(q31{0});
			}
		});

		it("all-passes exactly as the class's allPass", _ {
			for (const q31 m : kMoveabilities) {
				const std::vector<q31> in = quiet_noise(kLongRun, kSeed);
				const OnePole::Coefficients c = OnePole::coefficients({.moveability = m, .mode = Mode::ALL_PASS});
				OnePole::State s{};
				filters_reference::ReferenceOnePole pole;
				for (const q31 x : in) {
					expect(OnePole::step<Mode::ALL_PASS>(c, {}, s, x)).to_equal(pole.allPass(x, m));
				}
				expect(s.z).to_equal(pole.memory());
				expect(s.z).not_().to_equal(q31{0});
			}
		});

		it("reads the feedback exactly as the class does, at the same memory", _ {
			for (const q31 m : kMoveabilities) {
				const std::vector<q31> in = quiet_noise(kLongRun, kSeed);
				OnePole::State s{};
				filters_reference::ReferenceOnePole pole;
				for (const q31 x : in) {
					std::ignore = OnePole::step<Mode::LOW_PASS>({.m = m}, {}, s, x);
					std::ignore = pole.lowPass(x, m);
				}
				expect(s.z).to_equal(pole.memory());
				for (const q31 amount : kAmounts) {
					expect(OnePole::feedback(s, amount)).to_equal(pole.feedbackOutput(amount));
					expect(OnePole::feedback_unshifted(s, amount)).to_equal(pole.feedbackOutputUnshifted(amount));
				}
			}
		});

		it("reads the feedback from the memory alone, without stepping it", _ {
			const OnePole::State s{.z = 805326848};
			expect(OnePole::feedback(s, kRampMoveability)).to_equal(402663424);
			expect(OnePole::feedback_unshifted(s, kRampMoveability)).to_equal(100665856);
			expect(s.z).to_equal(805326848);
		});

		it("reads the feedback at full-scale memories, its shift wrapping as the class's does", _ {
			// z * amount at full scale is 2^30 - 1 or 2^30, which the shift by 2 carries out of q31.
			expect(OnePole::feedback({.z = kMin}, kMin)).to_equal(q31{0});
			expect(OnePole::feedback({.z = kMax}, kMax)).to_equal(q31{-4});
			expect(OnePole::feedback({.z = kMin + 1}, kMin + 1)).to_equal(q31{-4});
			expect(OnePole::feedback_unshifted({.z = kMin}, kMin)).to_equal(q31{1 << 30});
			expect(OnePole::feedback_unshifted({.z = kMax}, kMax)).to_equal(q31{(1 << 30) - 1});
			for (const q31 z : {kMin, kMax, kMin + 1}) {
				const OnePole::State s{.z = z};
				for (const q31 amount : kAmounts) {
					expect(OnePole::feedback(s, amount)).to_equal(wrapped_feedback(z, amount));
					expect(OnePole::feedback_unshifted(s, amount)).to_equal(multiply_32x32_rshift32_rounded(z, amount));
				}
				expect(s.z).to_equal(z);
			}
		});

		it("wraps as two's complement at full scale and at the extremes, in every mode", _ {
			const std::vector<q31> in = extreme_then_full_scale(kLongRun, kSeed);
			for (const q31 m : kMoveabilities) {
				OnePole::State low{};
				OnePole::State high{};
				OnePole::State all{};
				WrappingReference low_ref;
				WrappingReference high_ref;
				WrappingReference all_ref;
				std::size_t feedback_wraps = 0;
				for (const q31 x : in) {
					expect(OnePole::step<Mode::LOW_PASS>({.m = m}, {}, low, x)).to_equal(low_ref.low_pass(x, m));
					expect(OnePole::step<Mode::HIGH_PASS>({.m = m}, {}, high, x)).to_equal(high_ref.high_pass(x, m));
					expect(OnePole::step<Mode::ALL_PASS>({.m = m}, {}, all, x)).to_equal(all_ref.all_pass(x, m));

					for (const auto& [state, ref] : {std::pair{&low, &low_ref}, {&high, &high_ref}, {&all, &all_ref}}) {
						for (const q31 amount : kAmounts) {
							feedback_wraps += feedback_overflows(ref->memory, amount) ? 1 : 0;
							expect(OnePole::feedback(*state, amount)).to_equal(wrapped_feedback(ref->memory, amount));
							expect(OnePole::feedback_unshifted(*state, amount))
							    .to_equal(multiply_32x32_rshift32_rounded(ref->memory, amount));
						}
					}
				}
				expect(low.z).to_equal(low_ref.memory);
				expect(high.z).to_equal(high_ref.memory);
				expect(all.z).to_equal(all_ref.memory);
				// Each mode's own output arithmetic wraps, not only the x - z the modes share.
				expect(low_ref.wraps.input_less_memory).to_be_greater_than(std::size_t{100});
				expect(high_ref.wraps.input_less_output).to_be_greater_than(std::size_t{100});
				expect(all_ref.wraps.doubled_output).to_be_greater_than(std::size_t{100});
				expect(all_ref.wraps.doubled_less_input).to_be_greater_than(std::size_t{100});
				expect(feedback_wraps).to_be_greater_than(std::size_t{100});
			}
		});

		it("runs in Mono as a hand loop over the kernel, its state carried from block to block", _ {
			for (const Mode mode : kModes) {
				Mono<OnePole> mono{{.moveability = 1 << 29, .mode = mode}};
				const OnePole::Coefficients c = OnePole::coefficients({.moveability = 1 << 29, .mode = mode});
				OnePole::State s{};
				uint32_t seed = kSeed;
				for (const std::size_t n : kLengths) {
					const std::vector<q31> in = quiet_noise(n, seed++);
					std::vector<q31> got(n);
					mono.renderBlock(in, got);
					for (std::size_t i = 0; i < n; ++i) {
						expect(got[i]).to_equal(step_in(mode, c, s, in[i]));
					}
					expect(mono.state().z).to_equal(s.z);
				}
			}
		});

		it("keeps its memory across a mode switch between blocks", _ {
			Mono<OnePole> mono{{.moveability = 1 << 30, .mode = Mode::LOW_PASS}};
			OnePole::State s{};
			uint32_t seed = kSeed;
			for (const Mode mode : {Mode::LOW_PASS, Mode::HIGH_PASS, Mode::ALL_PASS, Mode::LOW_PASS}) {
				const q31 before = mono.state().z;
				mono.configure({.moveability = 1 << 30, .mode = mode});
				expect(mono.state().z).to_equal(before);
				const OnePole::Coefficients c = OnePole::coefficients({.moveability = 1 << 30, .mode = mode});
				const std::vector<q31> in = quiet_noise(64, seed++);
				std::vector<q31> got(64);
				mono.renderBlock(in, got);
				for (std::size_t i = 0; i < 64; ++i) {
					expect(got[i]).to_equal(step_in(mode, c, s, in[i]));
				}
				expect(mono.state().z).to_equal(s.z);
				expect(s.z).not_().to_equal(q31{0});
			}
		});
	});
});
// clang-format on

CPPSPEC_MAIN(one_pole_spec);
