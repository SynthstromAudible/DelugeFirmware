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

// tanh: the plain (non-antialiased) tanh saturation — the input driven by 2^drive, looked up in kTanhSmallTable and
// shifted down by drive + 2. The values were recorded from the app's getTanHUnknown / getTanH<N>. Both forms are also
// pinned against a reference that reads kTanhSmallTable entry by entry, at every drive 0 to 29, at the bounds of every
// saturation width and on random inputs, and on a recorded digest of those outputs.

#include "dsp-next/fixed/shaping/tanh.h"

#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

using deluge::dsp::fixed::kTanhCentre;
using deluge::dsp::fixed::kTanhSmallTable;
using deluge::dsp::fixed::tanh;

namespace {
constexpr int32_t kMin = std::numeric_limits<int32_t>::min();
constexpr int32_t kMax = std::numeric_limits<int32_t>::max();
constexpr std::array<int32_t, 7> kInputs{0, 1 << 28, -(1 << 28), 1 << 30, 12345678, kMin, kMax};

/// The highest drive either form takes: the output shift, drive + 2, stays below 32.
constexpr uint32_t kTopDrive = 29;
/// The FNV-1a offset basis: the digest of nothing.
constexpr uint64_t kDigestBasis = 14695981039346656037ull;
/// The entries of kTanhSmallTable, digested.
constexpr uint64_t kSmallTableDigest = 0x8ad143bb5a38ecffull;
/// Every output of tanh<D> for D 0 to 29 and then of tanh(x, d) for d 0 to 29 on the reference inputs, digested.
constexpr uint64_t kOutputDigest = 0x429beac262f256c0ull;

/// @p h, a 64-bit FNV-1a, extended by the 32-bit word @p w.
uint64_t digest(uint64_t h, int32_t w) {
	return (h ^ static_cast<uint32_t>(w)) * 1099511628211ull;
}

/// Zero, the extremes, the bounds of every saturation width from 1 to 32 bits, each ±64, then xorshift64 inputs.
std::vector<int32_t> reference_inputs() {
	std::vector<int32_t> v;
	for (int64_t d = -64; d <= 64; ++d) {
		const int64_t m = d < 0 ? -d : d;
		v.push_back(static_cast<int32_t>(d));
		v.push_back(static_cast<int32_t>(int64_t{kMax} - m));
		v.push_back(static_cast<int32_t>(int64_t{kMin} + m));
	}
	for (int bits = 1; bits <= 32; ++bits) {
		const int64_t high = (int64_t{1} << (bits - 1)) - 1;
		for (int64_t d = -64; d <= 64; ++d) {
			for (const int64_t bound : {high, -high - 1}) {
				const int64_t x = bound + d;
				if (x >= kMin && x <= kMax) {
					v.push_back(static_cast<int32_t>(x));
				}
			}
		}
	}
	uint64_t state = 0x9E37'79B9'7F4A'7C15ull;
	for (int i = 0; i < 4096; ++i) {
		state ^= state << 13;
		state ^= state >> 7;
		state ^= state << 17;
		v.push_back(static_cast<int32_t>(state >> 16));
	}
	return v;
}

/// @p x saturated to @p bits bits and shifted up by @p drive, wrapping, plus the centre.
uint32_t reference_position(int32_t x, int bits, uint32_t drive) {
	const int64_t high = (int64_t{1} << (bits - 1)) - 1;
	const int64_t clamped = x < -high - 1 ? -high - 1 : (x > high ? high : x);
	return (static_cast<uint32_t>(clamped) << drive) + kTanhCentre;
}

/// kTanhSmallTable read entry by entry at @p position (the top 8 bits the entry, the next 16 a Q16 blend toward the
/// next entry), shifted down by @p drive + 2.
int32_t reference_lookup(uint32_t position, uint32_t drive) {
	const uint32_t entry = position >> 24;
	const int32_t along = static_cast<int32_t>((position >> 8) & 0xFFFFu);
	return (kTanhSmallTable[entry] * (0x10000 - along) + kTanhSmallTable[entry + 1] * along) >> (drive + 2);
}

/// tanh<Drive> and its reference on every input, appended to @p plain and @p reference.
template <uint32_t Drive>
void run_fixed(const std::vector<int32_t>& inputs, std::vector<int32_t>& plain, std::vector<int32_t>& reference) {
	for (const int32_t x : inputs) {
		plain.push_back(tanh<Drive>(x));
		reference.push_back(reference_lookup(reference_position(x, 32 - static_cast<int>(Drive), Drive), Drive));
	}
}

/// run_fixed at every drive in @p Drives, in order.
template <uint32_t... Drives>
void run_fixed_at(std::integer_sequence<uint32_t, Drives...> /*drives*/, const std::vector<int32_t>& inputs,
                  std::vector<int32_t>& plain, std::vector<int32_t>& reference) {
	(run_fixed<Drives>(inputs, plain, reference), ...);
}

/// tanh(x, drive) and its reference on every input, appended to @p plain and @p reference: the runtime form passes
/// drive 0 unsaturated, and saturates at 32 − drive bits for drives 1 to 20 and at 12 bits past them.
void run_runtime(uint32_t drive, const std::vector<int32_t>& inputs, std::vector<int32_t>& plain,
                 std::vector<int32_t>& reference) {
	const int bits = drive == 0 ? 32 : (drive <= 20 ? 32 - static_cast<int>(drive) : 12);
	for (const int32_t x : inputs) {
		plain.push_back(tanh(x, drive));
		reference.push_back(reference_lookup(reference_position(x, bits, drive), drive));
	}
}
} // namespace

// clang-format off
describe tanh_spec("tanh", ${
	it("passes a signal undriven, at a quarter of its level", _ {
		constexpr std::array<int32_t, 7> expected{0, 248250368, -248250368, 517898240, 12345600, -536854528, 536854527};
		for (std::size_t i = 0; i < kInputs.size(); i++) {
			expect(tanh(kInputs[i], 0)).to_equal(expected[i]);
		}
	});
	it("saturates a signal driven by 2^2", _ {
		constexpr std::array<int32_t, 7> expected{0, 129474560, -129474560, 134213631, 12318382, -134213632, 134213631};
		for (std::size_t i = 0; i < kInputs.size(); i++) {
			expect(tanh(kInputs[i], 2)).to_equal(expected[i]);
		}
	});
	it("saturates a signal driven by 2^5", _ {
		constexpr std::array<int32_t, 7> expected{0, 16776703, -16776704, 16776703, 10518343, -16776704, 16776703};
		for (std::size_t i = 0; i < kInputs.size(); i++) {
			expect(tanh(kInputs[i], 5)).to_equal(expected[i]);
		}
	});
	it("saturates a signal driven by 2^8", _ {
		constexpr std::array<int32_t, 7> expected{0, 2097087, -2097088, 2097087, 2097087, -2097088, 2097087};
		for (std::size_t i = 0; i < kInputs.size(); i++) {
			expect(tanh(kInputs[i], 8)).to_equal(expected[i]);
		}
	});
	it("gives a compile-time drive the runtime drive's output", _ {
		for (int32_t input : kInputs) {
			expect(tanh<0>(input)).to_equal(tanh(input, 0));
			expect(tanh<2>(input)).to_equal(tanh(input, 2));
			expect(tanh<5>(input)).to_equal(tanh(input, 5));
		}
	});
	it("saturates a compile-time drive past 2^19 at its own width", _ {
		constexpr std::array<int32_t, 7> expected{0, 63, -64, 63, 63, -64, 63};
		for (std::size_t i = 0; i < kInputs.size(); i++) {
			expect(tanh<23>(kInputs[i])).to_equal(expected[i]);
		}
		expect(tanh<23>(100)).to_equal(58);
		expect(tanh<23>(-100)).to_equal(-59);
		expect(tanh<23>(200)).to_equal(63);
		expect(tanh<23>(-3000)).to_equal(-64);
	});
	it("reads kTanhSmallTable's recorded entries", _ {
		uint64_t h = kDigestBasis;
		for (const int16_t entry : kTanhSmallTable) {
			h = digest(h, entry);
		}
		expect(h).to_equal(kSmallTableDigest);
	});
	it("matches the reference at every compile-time drive, at each saturation width's bounds", _ {
		const std::vector<int32_t> inputs = reference_inputs();
		std::vector<int32_t> plain;
		std::vector<int32_t> reference;
		run_fixed_at(std::make_integer_sequence<uint32_t, kTopDrive + 1>{}, inputs, plain, reference);
		expect(plain.size()).to_equal(inputs.size() * (kTopDrive + 1));
		expect(plain).to_equal(reference);
	});
	it("matches the reference at every runtime drive, at each saturation width's bounds", _ {
		const std::vector<int32_t> inputs = reference_inputs();
		for (uint32_t drive = 0; drive <= kTopDrive; ++drive) {
			std::vector<int32_t> plain;
			std::vector<int32_t> reference;
			run_runtime(drive, inputs, plain, reference);
			expect(plain).to_equal(reference);
		}
	});
	it("gives the recorded outputs at every drive, compile-time and runtime", _ {
		const std::vector<int32_t> inputs = reference_inputs();
		std::vector<int32_t> plain;
		std::vector<int32_t> reference;
		run_fixed_at(std::make_integer_sequence<uint32_t, kTopDrive + 1>{}, inputs, plain, reference);
		for (uint32_t drive = 0; drive <= kTopDrive; ++drive) {
			run_runtime(drive, inputs, plain, reference);
		}
		uint64_t h = kDigestBasis;
		for (const int32_t out : plain) {
			h = digest(h, out);
		}
		expect(h).to_equal(kOutputDigest);
	});
	it("saturates a runtime drive past 2^19 at 12 bits", _ {
		constexpr std::array<int32_t, 7> expected{0, -1, 0, -1, -1, 0, -1};
		for (std::size_t i = 0; i < kInputs.size(); i++) {
			expect(tanh(kInputs[i], 23)).to_equal(expected[i]);
		}
	});
});

CPPSPEC_MAIN(tanh_spec);
