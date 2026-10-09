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

// The anti-aliased tanh. legacy::AntialiasedTanh, the class the compressor, the high-pass ladder and the saturation
// effect still hold, is pinned on exact reference values on both backends. The AntialiasedTanh kernel is pinned against
// the class, sample by sample from equal positions: at every drive the callers reach (0 to 20), on seeded noise at
// several levels and on alternating full-scale extremes; across a drive change in the middle of a block, which keeps
// the position as the class's setDrive does; and after seed and place, against a class constructed at the position
// those events set, at the drive extremes and at full-scale samples. The Tanh<Drive> kernel is pinned against tanh<>
// per sample at every drive 0 to 20.

#include "dsp-next/fixed/shaping/antialiased_tanh.h"

#include "dsp-next/core/concepts.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/fixed/noise/noise.h"
#include "dsp-next/fixed/shaping/legacy/antialiased_tanh.h"
#include "dsp-next/fixed/shaping/tanh.h"
#include "dsp-next/fixed/shaping/tanh_kernel.h"
#include "util/param_functions.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

using namespace deluge::dsp;
using namespace deluge::dsp::fixed;

namespace {

static_assert(processor_kernel<AntialiasedTanh>);
static_assert(!moded_kernel<AntialiasedTanh>);
static_assert(!std::is_polymorphic_v<AntialiasedTanh>);
static_assert(processor<Mono<AntialiasedTanh>>);
static_assert(AntialiasedTanh::coefficients({.drive = 5}).drive == 5);
static_assert(AntialiasedTanh::coefficients({}).drive == 0);
static_assert(AntialiasedTanh::State{}.position == kTanhCentre);
static_assert(kTanhCentre == 2147483648u);
static_assert(legacy::AntialiasedTanh::kSilent == kTanhCentre);

static_assert(processor_kernel<Tanh<0>>);
static_assert(processor_kernel<Tanh<3>>);
static_assert(processor_kernel<Tanh<20>>);
static_assert(!std::is_polymorphic_v<Tanh<3>>);
static_assert(processor<Mono<Tanh<3>>>);

/// The extremes of q31.
constexpr q31 kMin = std::numeric_limits<q31>::min();
constexpr q31 kMax = std::numeric_limits<q31>::max();
/// The highest drive a caller reaches: the saturation effect's voice variant at amount 15, 5 + 15.
constexpr uint32_t kMaxDrive = 20;
/// The samples the long comparisons run.
constexpr std::size_t kLongRun = 10'000;
/// The seed of every noise input.
constexpr uint32_t kSeed = 0x7A11'5EED;
/// The samples the events are pinned at: silence, the extremes, and mid values either side.
constexpr std::array<q31, 5> kEventSamples{0, kMin, kMax, 1 << 28, -(3 << 26)};
/// The raw positions place is pinned at: the table's two ends, its centre, and two between.
constexpr std::array<uint32_t, 5> kPlacePositions{0u, 0xFFFF'FFFFu, kTanhCentre, 0x1234'5678u, 0xC000'0001u};
/// The drives the events are pinned at: the extremes, the compressor's and the high-pass ladder's.
constexpr std::array<uint32_t, 5> kEventDrives{0, 1, 3, 8, kMaxDrive};

/// 64 samples alternating between INT32_MIN and INT32_MAX, then seeded noise at full scale, then at 2^-8 and 2^-16
/// of it, @p n samples each, so that every drive both saturates and stays in the curve's middle.
std::vector<q31> test_input(std::size_t n, uint32_t seed) {
	std::vector<q31> v;
	for (std::size_t i = 0; i < 64; ++i) {
		v.push_back(i % 2 == 0 ? kMin : kMax);
	}
	SeededNoise noise{seed};
	for (const int shift : {0, 8, 16}) {
		for (std::size_t i = 0; i < n; ++i) {
			v.push_back(noise.render() >> shift);
		}
	}
	return v;
}

/// Each sample's output and the position it leaves.
struct Run {
	std::vector<q31> out;
	std::vector<uint32_t> positions;
};

/// @p input through the kernel at @p drive from @p state, which is left where the last step put it.
Run run_kernel(uint32_t drive, AntialiasedTanh::State& state, const std::vector<q31>& input) {
	const AntialiasedTanh::Coefficients c = AntialiasedTanh::coefficients({.drive = drive});
	Run run;
	for (const q31 x : input) {
		run.out.push_back(AntialiasedTanh::step(c, EmptyBlock{}, state, x));
		run.positions.push_back(state.position);
	}
	return run;
}

/// @p input through the class @p curve at the drive it holds.
Run run_legacy(legacy::AntialiasedTanh& curve, const std::vector<q31>& input) {
	Run run;
	for (const q31 x : input) {
		run.out.push_back(curve.render(x));
		run.positions.push_back(curve.previous());
	}
	return run;
}

/// The compressor's seed: the position its first pre-gain sample takes at @p drive.
uint32_t compressor_seed(q31 sample, uint32_t drive) {
	return static_cast<uint32_t>(lshiftAndSaturateUnknown(sample, static_cast<uint8_t>(drive)))
	       + legacy::AntialiasedTanh::kSilent;
}

/// @p input through Tanh<Drive>, and through tanh<Drive>, appended to @p kernel and @p plain.
template <uint32_t Drive>
void run_tanh(const std::vector<q31>& input, std::vector<q31>& kernel, std::vector<q31>& plain) {
	typename Tanh<Drive>::State state{};
	const auto c = Tanh<Drive>::coefficients({});
	for (const q31 x : input) {
		kernel.push_back(Tanh<Drive>::step(c, EmptyBlock{}, state, x));
		plain.push_back(tanh<Drive>(x));
	}
}

/// run_tanh at every drive in @p Drives, in order.
template <uint32_t... Drives>
void run_tanh_at(std::integer_sequence<uint32_t, Drives...> /*drives*/, const std::vector<q31>& input,
                 std::vector<q31>& kernel, std::vector<q31>& plain) {
	(run_tanh<Drives>(input, kernel, plain), ...);
}

/// The outputs of one fresh class at @p drive for the five inputs every legacy case uses, in order.
std::vector<int32_t> legacy_outputs(uint32_t drive) {
	legacy::AntialiasedTanh tanh{drive};
	std::vector<int32_t> out;
	for (int32_t in : {0, 1 << 28, -(1 << 28), 1 << 30, 12345678}) {
		out.push_back(tanh.render(in));
	}
	return out;
}

} // namespace

// clang-format off
describe legacy_antialiased_tanh("legacy::AntialiasedTanh", ${
	it("saturates a sequence at drive 3 exactly", _ {
		expect(legacy_outputs(3)).to_equal(std::vector<int32_t>{0, 55521277, -1, -9, 57900622});
	});
	it("saturates a sequence at drive 5 exactly", _ {
		expect(legacy_outputs(5)).to_equal(std::vector<int32_t>{0, 13880319, -1, -3, 15726067});
	});
	it("saturates a sequence at drive 8 exactly", _ {
		expect(legacy_outputs(8)).to_equal(std::vector<int32_t>{0, 1735039, -1, -1, 2097087});
	});
	it("remembers the last working value", _ {
		legacy::AntialiasedTanh tanh{3};
		for (int32_t in : {0, 1 << 28, -(1 << 28), 1 << 30, 12345678}) {
			std::ignore = tanh.render(in);
		}
		expect(tanh.previous()).to_equal(2246249072u);
	});
	it("starts from the value it is given", _ {
		legacy::AntialiasedTanh fromZero{5, 0};
		legacy::AntialiasedTanh fromSilence{5};
		expect(fromZero.render(1 << 28)).not_().to_equal(fromSilence.render(1 << 28));
	});
	it("renders a block as it renders each sample", _ {
		legacy::AntialiasedTanh bySample{5};
		legacy::AntialiasedTanh byBlock{5};
		const std::vector<int32_t> in{1 << 20, 1 << 26, -(1 << 27), 1 << 29};
		std::vector<int32_t> out(in.size());
		byBlock.renderBlock(in, out);
		for (size_t i = 0; i < in.size(); i++) {
			expect(out[i]).to_equal(bySample.render(in[i]));
		}
	});
});
// clang-format on

// clang-format off
describe antialiased_tanh_kernel("AntialiasedTanh", ${
	it("steps as the class renders, at every drive, from silence", _ {
		const std::vector<q31> input = test_input(kLongRun, kSeed);
		for (uint32_t drive = 0; drive <= kMaxDrive; ++drive) {
			AntialiasedTanh::State state{};
			legacy::AntialiasedTanh curve{drive};
			{
				const Run kernel = run_kernel(drive, state, input);
				const Run legacy = run_legacy(curve, input);
				expect(kernel.out).to_equal(legacy.out);
				expect(kernel.positions).to_equal(legacy.positions);
			}
		}
	});
	it("keeps the position across a drive change in the middle of a block, as the class's setDrive does", _ {
		constexpr std::size_t kChangeAt = 37;
		const std::vector<q31> input = test_input(kLongRun / 10, kSeed + 1);
		const std::vector<q31> before(input.begin(), input.begin() + kChangeAt);
		const std::vector<q31> after(input.begin() + kChangeAt, input.end());
		for (const auto& [from, to] : std::array<std::pair<uint32_t, uint32_t>, 6>{
		         {{3, 8}, {8, 3}, {0, kMaxDrive}, {kMaxDrive, 0}, {5, 6}, {1, 0}}}) {
			AntialiasedTanh::State state{};
			legacy::AntialiasedTanh curve{from};
			{
				const Run kernel = run_kernel(from, state, before);
				const Run legacy = run_legacy(curve, before);
				expect(kernel.out).to_equal(legacy.out);
				expect(kernel.positions).to_equal(legacy.positions);
			}
			curve.setDrive(to);
			{
				const Run kernel = run_kernel(to, state, after);
				const Run legacy = run_legacy(curve, after);
				expect(kernel.out).to_equal(legacy.out);
				expect(kernel.positions).to_equal(legacy.positions);
			}
		}
	});
	it("keeps the position across a drive change between blocks in Mono, as the class's setDrive does", _ {
		constexpr uint32_t kFrom = 3;
		constexpr uint32_t kTo = 11;
		const std::vector<q31> input = test_input(kLongRun / 10, kSeed + 2);
		const std::size_t half = input.size() / 2;
		Mono<AntialiasedTanh> mono{{.drive = kFrom}};
		std::vector<q31> out(input.size());
		mono.renderBlock(std::span<const q31>{input.data(), half}, std::span<q31>{out.data(), half});
		mono.configure({.drive = kTo});
		mono.renderBlock(std::span<const q31>{input.data() + half, input.size() - half},
		                 std::span<q31>{out.data() + half, input.size() - half});
		legacy::AntialiasedTanh curve{kFrom};
		for (std::size_t i = 0; i < input.size(); ++i) {
			if (i == half) {
				curve.setDrive(kTo);
			}
			expect(out[i]).to_equal(curve.render(input[i]));
		}
		expect(mono.state().position).to_equal(curve.previous());
	});
	it("seeds the position the compressor constructs from a first sample, at the drive extremes and full scale", _ {
		const std::vector<q31> input = test_input(kLongRun / 10, kSeed + 3);
		for (const uint32_t drive : kEventDrives) {
			const AntialiasedTanh::Coefficients c = AntialiasedTanh::coefficients({.drive = drive});
			for (const q31 sample : kEventSamples) {
				AntialiasedTanh::State state{.position = 0x5555'5555u};
				AntialiasedTanh::seed(c, state, sample);
				expect(state.position).to_equal(compressor_seed(sample, drive));
				legacy::AntialiasedTanh fresh{drive, compressor_seed(sample, drive)};
				{
				const Run kernel = run_kernel(drive, state, input);
				const Run legacy = run_legacy(fresh, input);
				expect(kernel.out).to_equal(legacy.out);
				expect(kernel.positions).to_equal(legacy.positions);
			}
			}
		}
	});
	it("seeds the position a step on the same sample leaves", _ {
		for (const uint32_t drive : kEventDrives) {
			const AntialiasedTanh::Coefficients c = AntialiasedTanh::coefficients({.drive = drive});
			for (const q31 sample : kEventSamples) {
				AntialiasedTanh::State seeded{};
				AntialiasedTanh::seed(c, seeded, sample);
				AntialiasedTanh::State stepped{};
				std::ignore = AntialiasedTanh::step(c, EmptyBlock{}, stepped, sample);
				expect(seeded.position).to_equal(stepped.position);
			}
		}
	});
	it("seeds within 12 bits of the centre at drive 0, where the drive's shift saturates at 12 bits", _ {
		const AntialiasedTanh::Coefficients c = AntialiasedTanh::coefficients({.drive = 0});
		AntialiasedTanh::State state{};
		AntialiasedTanh::seed(c, state, kMax);
		expect(state.position).to_equal(kTanhCentre + 2047u);
		AntialiasedTanh::seed(c, state, kMin);
		expect(state.position).to_equal(kTanhCentre - 2048u);
	});
	it("places a raw position as the class constructs one, at the table's ends and the high-pass ladder's bypass", _ {
		const std::vector<q31> input = test_input(kLongRun / 10, kSeed + 4);
		std::vector<uint32_t> positions(kPlacePositions.begin(), kPlacePositions.end());
		for (const q31 a : kEventSamples) {
			positions.push_back(static_cast<uint32_t>(lshiftAndSaturate<2>(a)) + kTanhCentre);
		}
		for (const uint32_t drive : kEventDrives) {
			for (const uint32_t position : positions) {
				AntialiasedTanh::State state{};
				AntialiasedTanh::place(state, position);
				expect(state.position).to_equal(position);
				legacy::AntialiasedTanh fresh{drive, position};
				{
				const Run kernel = run_kernel(drive, state, input);
				const Run legacy = run_legacy(fresh, input);
				expect(kernel.out).to_equal(legacy.out);
				expect(kernel.positions).to_equal(legacy.positions);
			}
			}
		}
	});
});

describe tanh_kernel("Tanh<Drive>", ${
	it("steps as tanh<Drive> at every drive", _ {
		const std::vector<q31> input = test_input(kLongRun, kSeed + 5);
		std::vector<q31> kernel;
		std::vector<q31> plain;
		run_tanh_at(std::make_integer_sequence<uint32_t, kMaxDrive + 1>{}, input, kernel, plain);
		expect(kernel.size()).to_equal(input.size() * (kMaxDrive + 1));
		expect(kernel).to_equal(plain);
	});
	it("needs no state: a block in Mono is tanh<3> sample by sample", _ {
		const std::vector<q31> input = test_input(kLongRun / 10, kSeed + 6);
		Mono<Tanh<3>> mono{};
		std::vector<q31> out(input.size());
		mono.renderBlock(std::span<const q31>{input.data(), input.size()}, std::span<q31>{out.data(), out.size()});
		for (std::size_t i = 0; i < input.size(); ++i) {
			expect(out[i]).to_equal(tanh<3>(input[i]));
		}
	});
});
// clang-format on

CPPSPEC_MAIN(legacy_antialiased_tanh, antialiased_tanh_kernel, tanh_kernel);
