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
// the class, sample by sample from equal positions: at every drive the callers reach (0 to 20) and at two past them
// (25 and 30), on seeded noise at several levels and on alternating full-scale extremes; across a drive change in the
// middle of a block, which keeps the position as the class's setDrive does; and after seed and place, against a class
// constructed at the position those events set, at the drive extremes and at full-scale samples. The Tanh<Drive> kernel
// is pinned against tanh<> per sample at every drive 0 to 20. Both the kernel and the class are also pinned against a
// reference that reads kTanhTable entry by entry and saturates by the documented rule, at every drive 0 to 30: at the
// bounds of every saturation width ±64, each stepped from the previous input and from a random position, and on random
// inputs from random positions; the kernel's outputs there, and the table's entries, are also pinned on recorded
// digests.

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
static_assert(AntialiasedTanh::coefficients({}).floor == AntialiasedTanh::Coefficients{}.floor);
static_assert(AntialiasedTanh::coefficients({}).ceiling == AntialiasedTanh::Coefficients{}.ceiling);
static_assert(AntialiasedTanh::coefficients({.drive = 1}).floor == -(1 << 30));
static_assert(AntialiasedTanh::coefficients({.drive = 1}).ceiling == (1 << 30) - 1);
static_assert(AntialiasedTanh::coefficients({.drive = 20}).ceiling == 2047);
static_assert(AntialiasedTanh::coefficients({.drive = 21}).ceiling == 2047);
static_assert(AntialiasedTanh::coefficients({.drive = 30}).floor == -2048);
static_assert(AntialiasedTanh::State{}.position == kTanhCentre);
static_assert(kTanhCentre == 2147483648u);
static_assert(legacy::AntialiasedTanh::kSilent == kTanhCentre);

static_assert(processor_kernel<Tanh<0>>);
static_assert(processor_kernel<Tanh<3>>);
static_assert(processor_kernel<Tanh<20>>);
static_assert(!std::is_polymorphic_v<Tanh<3>>);

/// Whether Tanh<Drive> is a valid type: its output shift, Drive + 2, must stay below 32.
template <uint32_t Drive>
concept tanh_drive_allowed = requires { typename Tanh<Drive>; };
static_assert(tanh_drive_allowed<29>);
static_assert(!tanh_drive_allowed<30>);
static_assert(!tanh_drive_allowed<31>);
static_assert(processor<Mono<Tanh<3>>>);

/// The extremes of q31.
constexpr q31 kMin = std::numeric_limits<q31>::min();
constexpr q31 kMax = std::numeric_limits<q31>::max();
/// The highest drive a caller reaches: the saturation effect's voice variant at amount 15, 5 + 15.
constexpr uint32_t kMaxDrive = 20;
/// Drives past the callers' range, up to the documented 30, where the 12-bit saturation's shift wraps.
constexpr std::array<uint32_t, 2> kWrappingDrives{25, 30};
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

/// The highest drive the kernel documents.
constexpr uint32_t kTopDrive = 30;

/// The FNV-1a offset basis: the digest of nothing.
constexpr uint64_t kDigestBasis = 14695981039346656037ull;
/// The entries of kTanhTable, row by row, digested.
constexpr uint64_t kTanhTableDigest = 0x210975209ceed53bull;
/// Every output of the kernel's sweeps at drives 0 to 30, in order, digested.
constexpr uint64_t kSweepDigest = 0x2c5f42e65931dcc9ull;

/// @p h, a 64-bit FNV-1a, extended by the 32-bit word @p w.
uint64_t digest(uint64_t h, int32_t w) {
	return (h ^ static_cast<uint32_t>(w)) * 1099511628211ull;
}
/// @p h extended by each of @p words.
uint64_t digest(uint64_t h, std::span<const int32_t> words) {
	for (const int32_t w : words) {
		h = digest(h, w);
	}
	return h;
}

/// A xorshift64 stream, for positions and inputs the noise generator's range does not reach.
struct XorShift {
	uint64_t state;
	uint32_t next() {
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		return static_cast<uint32_t>(state >> 16);
	}
};

/// The inputs at which a drive's saturation starts, stops or wraps: zero, the extremes, and the bounds of every
/// saturation width from 1 to 32 bits, each ±64.
std::vector<q31> saturation_edges() {
	std::vector<q31> e;
	for (int64_t d = -64; d <= 64; ++d) {
		const int64_t m = d < 0 ? -d : d;
		e.push_back(static_cast<q31>(d));
		e.push_back(static_cast<q31>(int64_t{kMax} - m));
		e.push_back(static_cast<q31>(int64_t{kMin} + m));
	}
	for (int bits = 1; bits <= 32; ++bits) {
		const int64_t high = (int64_t{1} << (bits - 1)) - 1;
		for (int64_t d = -64; d <= 64; ++d) {
			for (const int64_t bound : {high, -high - 1}) {
				const int64_t v = bound + d;
				if (v >= kMin && v <= kMax) {
					e.push_back(static_cast<q31>(v));
				}
			}
		}
	}
	return e;
}

/// The reference's position for @p x at @p drive, from the documented rule: x saturated to 32 − drive bits for drives
/// 1 to 20 and to 12 bits otherwise, shifted up by the drive (wrapping), plus the centre.
uint32_t reference_position(uint32_t drive, q31 x) {
	const int bits = (drive >= 1 && drive <= 20) ? 32 - static_cast<int>(drive) : 12;
	const int64_t high = (int64_t{1} << (bits - 1)) - 1;
	const int64_t clamped = x < -high - 1 ? -high - 1 : (x > high ? high : x);
	return (static_cast<uint32_t>(clamped) << drive) + kTanhCentre;
}

/// The reference's output for a step from @p previous to @p position at @p drive: kTanhTable read entry by entry,
/// each row blended at a Q16 weight, the two rows at a Q31 weight with 64-bit products, shifted down by drive + 1.
q31 reference_output(uint32_t drive, uint32_t previous, uint32_t position) {
	const uint32_t row = previous >> 26;    // 64 row intervals: the top 6 bits
	const uint32_t column = position >> 25; // 128 column intervals: the top 7 bits
	const int64_t along = (position >> 9) & 0xFFFFu;
	const auto row_value = [&](uint32_t r) {
		return static_cast<int32_t>(kTanhTable[r][column] * (0x10000 - along) + kTanhTable[r][column + 1] * along);
	};
	const int64_t toward_upper = (int64_t{previous} << 5) & 0x7FFF'FFFF;
	const int64_t toward_lower = 0x7FFF'FFFF - toward_upper;
	const auto lower = static_cast<int32_t>((row_value(row) * toward_lower) >> 32);
	const auto upper = static_cast<int32_t>((row_value(row + 1) * toward_upper) >> 32);
	return (lower + upper) >> (drive + 1);
}

/// The kernel, as a sweep drives it.
struct KernelCurve {
	AntialiasedTanh::Coefficients c;
	AntialiasedTanh::State s{};
	explicit KernelCurve(uint32_t drive) : c{AntialiasedTanh::coefficients({.drive = drive})} {}
	q31 step(q31 x) { return AntialiasedTanh::step(c, EmptyBlock{}, s, x); }
	void place(uint32_t p) { AntialiasedTanh::place(s, p); }
	[[nodiscard]] uint32_t position() const { return s.position; }
};

/// The class, as a sweep drives it: a place constructs it afresh at the position.
struct LegacyCurve {
	uint32_t drive;
	legacy::AntialiasedTanh curve;
	explicit LegacyCurve(uint32_t d) : drive{d}, curve{d} {}
	q31 step(q31 x) { return curve.render(x); }
	void place(uint32_t p) { curve = legacy::AntialiasedTanh{drive, p}; }
	[[nodiscard]] uint32_t position() const { return curve.previous(); }
};

/// The reference, as a sweep drives it.
struct ReferenceCurve {
	uint32_t drive;
	uint32_t previous = kTanhCentre;
	explicit ReferenceCurve(uint32_t d) : drive{d} {}
	q31 step(q31 x) {
		const uint32_t position = reference_position(drive, x);
		const q31 out = reference_output(drive, previous, position);
		previous = position;
		return out;
	}
	void place(uint32_t p) { previous = p; }
	[[nodiscard]] uint32_t position() const { return previous; }
};

/// @p Curve at @p drive from silence through: each edge stepped from the previous edge and again from a random
/// position; full-range random inputs, from a random position every 16 steps; and random inputs at random levels.
template <typename Curve>
Run sweep(uint32_t drive, const std::vector<q31>& edges) {
	Curve curve{drive};
	XorShift rng{0x9E37'79B9'7F4A'7C15ull + drive};
	Run run;
	const auto step = [&](q31 x) {
		run.out.push_back(curve.step(x));
		run.positions.push_back(curve.position());
	};
	for (const q31 x : edges) {
		step(x);
		curve.place(rng.next());
		step(x);
	}
	for (uint32_t i = 0; i < 4096; ++i) {
		if (i % 16 == 0) {
			curve.place(rng.next());
		}
		step(static_cast<q31>(rng.next()));
	}
	for (uint32_t i = 0; i < 2048; ++i) {
		const uint32_t level = rng.next() & 31u;
		step(static_cast<q31>(rng.next()) >> level);
	}
	return run;
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
			const Run kernel = run_kernel(drive, state, input);
			const Run reference = run_legacy(curve, input);
			expect(kernel.out).to_equal(reference.out);
			expect(kernel.positions).to_equal(reference.positions);
		}
	});
	it("steps as the class renders past the callers' drives, where the shift wraps", _ {
		const std::vector<q31> input = test_input(kLongRun, kSeed);
		for (const uint32_t drive : kWrappingDrives) {
			AntialiasedTanh::State state{};
			legacy::AntialiasedTanh curve{drive};
			const Run kernel = run_kernel(drive, state, input);
			const Run reference = run_legacy(curve, input);
			expect(kernel.out).to_equal(reference.out);
			expect(kernel.positions).to_equal(reference.positions);
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
			const Run kernel_before = run_kernel(from, state, before);
			const Run reference_before = run_legacy(curve, before);
			expect(kernel_before.out).to_equal(reference_before.out);
			expect(kernel_before.positions).to_equal(reference_before.positions);
			curve.setDrive(to);
			const Run kernel_after = run_kernel(to, state, after);
			const Run reference_after = run_legacy(curve, after);
			expect(kernel_after.out).to_equal(reference_after.out);
			expect(kernel_after.positions).to_equal(reference_after.positions);
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
				const Run kernel = run_kernel(drive, state, input);
				const Run reference = run_legacy(fresh, input);
				expect(kernel.out).to_equal(reference.out);
				expect(kernel.positions).to_equal(reference.positions);
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
				const Run kernel = run_kernel(drive, state, input);
				const Run reference = run_legacy(fresh, input);
				expect(kernel.out).to_equal(reference.out);
				expect(kernel.positions).to_equal(reference.positions);
			}
		}
	});
});

describe antialiased_tanh_reference("AntialiasedTanh against its definition", ${
	it("reads kTanhTable's recorded entries", _ {
		uint64_t h = kDigestBasis;
		for (std::size_t r = 0; r < 65; ++r) {
			for (std::size_t c = 0; c < 129; ++c) {
				h = digest(h, int32_t{kTanhTable[r][c]});
			}
		}
		expect(h).to_equal(kTanhTableDigest);
	});
	it("steps as the reference at every drive, at each saturation width's bounds and across the table", _ {
		const std::vector<q31> edges = saturation_edges();
		for (uint32_t drive = 0; drive <= kTopDrive; ++drive) {
			const Run kernel = sweep<KernelCurve>(drive, edges);
			const Run reference = sweep<ReferenceCurve>(drive, edges);
			expect(kernel.out).to_equal(reference.out);
			expect(kernel.positions).to_equal(reference.positions);
		}
	});
	it("renders as the reference in the class at every drive, at each saturation width's bounds", _ {
		const std::vector<q31> edges = saturation_edges();
		for (uint32_t drive = 0; drive <= kTopDrive; ++drive) {
			const Run legacy = sweep<LegacyCurve>(drive, edges);
			const Run reference = sweep<ReferenceCurve>(drive, edges);
			expect(legacy.out).to_equal(reference.out);
			expect(legacy.positions).to_equal(reference.positions);
		}
	});
	it("steps the recorded outputs at every drive", _ {
		const std::vector<q31> edges = saturation_edges();
		uint64_t h = kDigestBasis;
		for (uint32_t drive = 0; drive <= kTopDrive; ++drive) {
			const Run kernel = sweep<KernelCurve>(drive, edges);
			h = digest(h, kernel.out);
		}
		expect(h).to_equal(kSweepDigest);
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

CPPSPEC_MAIN(legacy_antialiased_tanh, antialiased_tanh_kernel, antialiased_tanh_reference, tanh_kernel);
