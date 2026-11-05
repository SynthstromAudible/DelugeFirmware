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

// Spec for deluge::dsp::fixed::WavetableCycleChunker — the wave-index cycle chunker the two wavetable
// oscillators share. It runs entirely on a synthetic in-memory WaveTableData (no SD card / storage),
// asserting the chunk tiling + cycle walk in isolation; the per-sample sinc render is golden-covered.
// Dual-arch (x86 SIMDe + ARM/QEMU) via CTest — pure index logic, kept dual-arch for parity.

#include "dsp-next/core/buffer_view.h"
#include "dsp-next/fixed/wavetable/wavetable_cycle_chunker.h"
#include "dsp-next/fixed/wavetable/wavetable_oscillator.h"
#include "raw_words.h"
#include <array>
#include <cmath>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace {
using deluge::dsp::Q;
using deluge::dsp::fixed::CycleChunk;
using deluge::dsp::fixed::WaveTableBand;
using deluge::dsp::fixed::WavetableCycleChunker;
using deluge::dsp::fixed::WaveTableData;

constexpr int32_t kBlock = 64;
constexpr uint32_t kPlayRate = 0x00100000; // the band and kernel it picks; the level spec also renders at it

// One band that covers every cycle, so the band walk always finds data. dataAccessAddress is null —
// these specs assert chunk geometry, never render.
constexpr WaveTableBand kBand{
    /*maxPhaseIncrement=*/0xFFFFFFFFu, /*fromCycleNumber=*/-1,   /*toCycleNumber=*/1000,
    /*cycleSizeNoDuplicates=*/256,     /*cycleSizeMagnitude=*/8, /*dataAccessAddress=*/nullptr};

WaveTableData make_table(int32_t num_cycles, int32_t transitions_magnitude, int32_t wave_index_multiplier) {
	return WaveTableData{num_cycles, transitions_magnitude, wave_index_multiplier, &kBand, sizeof(WaveTableBand), 1};
}

std::vector<CycleChunk> collect(const WaveTableData& table, std::span<int32_t> output, int32_t wave_index_increment) {
	WavetableCycleChunker chunker{table, deluge::dsp::typed_view(output), kPlayRate, /*wave_index=*/0,
	                              wave_index_increment};
	std::vector<CycleChunk> chunks;
	while (std::optional<CycleChunk> chunk = chunker.next()) {
		chunks.push_back(*chunk);
	}
	return chunks;
}
} // namespace

namespace morph_reference {
std::uint32_t xorshift(std::uint32_t& s) {
	s ^= s << 13;
	s ^= s >> 17;
	s ^= s << 5;
	return s;
}

/// The morph in plain integers: value1 plus the difference times the unsigned blend, the 64-bit product's high word
/// (floored), all in value1's Q30 words. The difference and the sum are taken through uint32_t, so they wrap where
/// the plain arithmetic would overflow.
int32_t raw_morph(int32_t value1, int32_t value2, uint32_t blend) {
	const auto difference = static_cast<int32_t>(static_cast<uint32_t>(value2) - static_cast<uint32_t>(value1));
	const auto share = static_cast<int32_t>((static_cast<int64_t>(difference) * static_cast<int64_t>(blend)) >> 32);
	return static_cast<int32_t>(static_cast<uint32_t>(value1) + static_cast<uint32_t>(share));
}

/// Mismatches between the typed morph and the plain one over 2^16 random values and blends, the rails and the blend's
/// ends included.
std::size_t mismatches() {
	constexpr std::array<int32_t, 5> kValues{0, 1, -1, INT32_MIN, INT32_MAX};
	constexpr std::array<uint32_t, 4> kBlends{0, 0x8000'0000U, 0xFFFF'FFFFU, 1};
	std::size_t bad = 0;
	std::uint32_t s = 0x6d6f'7270u;
	for (int k = 0; k < (1 << 16); ++k) {
		auto value1 = static_cast<int32_t>(xorshift(s));
		auto value2 = static_cast<int32_t>(xorshift(s));
		auto blend = xorshift(s);
		if (k < static_cast<int>(kValues.size() * kValues.size() * kBlends.size())) {
			value1 = kValues[k % kValues.size()];
			value2 = kValues[(k / kValues.size()) % kValues.size()];
			blend = kBlends[k / (kValues.size() * kValues.size())];
		}
		const auto got = deluge::dsp::fixed::wavetable_detail::morph(Q<30>::from_raw(value1), Q<30>::from_raw(value2),
		                                                             deluge::dsp::UQ<32>::from_raw(blend));
		bad += got.raw() != raw_morph(value1, value2, blend) ? 1 : 0;
	}
	return bad;
}
/// Values whose morph with themselves, at any blend, is not the value: a morph between two equal cycles must play
/// at the single cycle's level, so its result is in the same Q30 words.
std::size_t level_changes() {
	std::size_t bad = 0;
	std::uint32_t s = 0x6c65'7665u;
	for (int k = 0; k < (1 << 12); ++k) {
		const auto value = Q<30>::from_raw(static_cast<int32_t>(xorshift(s)));
		const auto got =
		    deluge::dsp::fixed::wavetable_detail::morph(value, value, deluge::dsp::UQ<32>::from_raw(xorshift(s)));
		bad += got != value ? 1 : 0;
	}
	return bad;
}
} // namespace morph_reference

namespace level_reference {
using deluge::dsp::UQ;
using deluge::dsp::fixed::kWavetableDuplicateSamplesAtCycleEnd;
using deluge::dsp::fixed::WavetableOscillator;

constexpr int32_t kCycleSize = 256;
constexpr int32_t kStride = kCycleSize + kWavetableDuplicateSamplesAtCycleEnd;

/// One cycle of a two-harmonic wave, as the band stores it: the cycle, then its first samples again.
void write_cycle(int16_t* out) {
	for (int32_t i = 0; i < kStride; ++i) {
		const double x = 2.0 * 3.141592653589793 * (i % kCycleSize) / kCycleSize;
		out[i] = static_cast<int16_t>(16000.0 * std::sin(x) + 6000.0 * std::sin(3.0 * x));
	}
}

/// Frames where a two-cycle table whose cycles are equal renders differently from the one-cycle table of that cycle,
/// the wave index held at @p wave_index and moving by @p wave_index_increment per sample.
std::size_t differences(int32_t wave_index, int32_t wave_index_increment) {
	static std::array<int16_t, 2 * kStride> data{};
	write_cycle(data.data());
	write_cycle(data.data() + kStride);
	const WaveTableBand band{0xFFFFFFFFu, -1, 2, kCycleSize, 8, data.data()};
	const WaveTableData single{1, 0, 0, &band, sizeof(WaveTableBand), 1};
	// As the loader sets a two-cycle table: one transition, its magnitude 1, the index multiplier 1 << 30.
	const WaveTableData morphing{2, 1, 1 << 30, &band, sizeof(WaveTableBand), 1};

	std::array<int32_t, 128> want{};
	std::array<int32_t, 128> got{};
	const auto increment = UQ<32>::from_raw(kPlayRate);
	WavetableOscillator{single, UQ<32>{}, increment, 0, 0}.render_block(deluge::dsp::typed_view(std::span{want}));
	WavetableOscillator{morphing, UQ<32>{}, increment, wave_index, wave_index_increment}.render_block(
	    deluge::dsp::typed_view(std::span{got}));
	std::size_t bad = 0;
	for (std::size_t i = 0; i < want.size(); ++i) {
		bad += want[i] != got[i] ? 1 : 0;
	}
	return bad;
}
} // namespace level_reference

// clang-format off
describe wavetable_cycle_chunker("deluge::dsp::fixed WavetableCycleChunker", ${
	it("morphs between two cycles as the plain high-word blend does, bit for bit", _ {
		expect(morph_reference::mismatches()).to_equal(std::size_t{0});
	});
	it("plays a morph between two equal cycles at the single cycle's level", _ {
		expect(morph_reference::level_changes()).to_equal(std::size_t{0});
	});
	it("renders a two-cycle table of equal cycles as the single cycle, held or sweeping", _ {
		expect(level_reference::differences(0x20000000, 0)).to_equal(std::size_t{0});
		expect(level_reference::differences(0, 0x00100000)).to_equal(std::size_t{0});
	});
	it("yields one whole-block single-cycle chunk for a single-cycle wavetable", _ {
		std::array<int32_t, kBlock> buffer{};
		WaveTableData table = make_table(/*num_cycles=*/1, /*transitions_magnitude=*/0, /*wave_index_multiplier=*/0x40000000);
		std::vector<CycleChunk> chunks = collect(table, std::span<int32_t>{buffer}, /*wave_index_increment=*/0);
		expect(chunks.size()).to_equal(static_cast<std::size_t>(1));
		expect(chunks[0].single_cycle).to_be_truthy();
		expect(chunks[0].band != nullptr).to_be_truthy();
		expect(static_cast<int32_t>(chunks[0].output.size())).to_equal(kBlock);
	});

	it("yields one whole-block chunk for a static (non-sweeping) morph", _ {
		std::array<int32_t, kBlock> buffer{};
		WaveTableData table = make_table(/*num_cycles=*/4, /*transitions_magnitude=*/2, /*wave_index_multiplier=*/0x40000000);
		std::vector<CycleChunk> chunks = collect(table, std::span<int32_t>{buffer}, /*wave_index_increment=*/0);
		expect(chunks.size()).to_equal(static_cast<std::size_t>(1));
		expect(!chunks[0].single_cycle).to_be_truthy();
		expect(chunks[0].first_cycle).to_equal(0);
		expect(static_cast<int32_t>(chunks[0].output.size())).to_equal(kBlock);
	});

	it("tiles the block into ascending-cycle chunks for a sweeping morph", _ {
		std::array<int32_t, kBlock> buffer{};
		WaveTableData table = make_table(/*num_cycles=*/4, /*transitions_magnitude=*/2, /*wave_index_multiplier=*/0x40000000);
		// A wave-index sweep that crosses a couple of cycle boundaries within the block.
		std::vector<CycleChunk> chunks = collect(table, std::span<int32_t>{buffer}, /*wave_index_increment=*/0x02000000);
		expect(chunks.size() > 1).to_be_truthy();
		int32_t total = 0;
		for (std::size_t k = 0; k < chunks.size(); ++k) {
			expect(!chunks[k].single_cycle).to_be_truthy();
			expect(chunks[k].band != nullptr).to_be_truthy();
			expect(static_cast<int32_t>(chunks[k].output.size()) >= 1).to_be_truthy();
			if (k > 0) {
				// cycles only go up as the index sweeps, and the chunks tile the block contiguously
				expect(chunks[k].first_cycle >= chunks[k - 1].first_cycle).to_be_truthy();
				expect(chunks[k].output.data())
				    .to_equal(chunks[k - 1].output.data() + chunks[k - 1].output.size());
			}
			total += static_cast<int32_t>(chunks[k].output.size());
		}
		expect(total).to_equal(kBlock);
	});
});
// clang-format on

CPPSPEC_MAIN(wavetable_cycle_chunker);
