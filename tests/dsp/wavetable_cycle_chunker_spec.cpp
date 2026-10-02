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

#include "dsp-next/fixed/wavetable/wavetable_cycle_chunker.h"
#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <optional>
#include <vector>

namespace {
using deluge::dsp::fixed::Buffer;
using deluge::dsp::fixed::CycleChunk;
using deluge::dsp::fixed::WaveTableBand;
using deluge::dsp::fixed::WavetableCycleChunker;
using deluge::dsp::fixed::WaveTableData;

constexpr int32_t kBlock = 64;
constexpr uint32_t kPlayRate = 0x00100000; // only affects which band/kernel is picked

// One band that covers every cycle, so the band walk always finds data. dataAccessAddress is null —
// these specs assert chunk geometry, never render.
constexpr WaveTableBand kBand{
    /*maxPhaseIncrement=*/0xFFFFFFFFu, /*fromCycleNumber=*/-1,   /*toCycleNumber=*/1000,
    /*cycleSizeNoDuplicates=*/256,     /*cycleSizeMagnitude=*/8, /*dataAccessAddress=*/nullptr};

WaveTableData make_table(int32_t num_cycles, int32_t transitions_magnitude, int32_t wave_index_multiplier) {
	return WaveTableData{num_cycles, transitions_magnitude, wave_index_multiplier, &kBand, sizeof(WaveTableBand), 1};
}

std::vector<CycleChunk> collect(const WaveTableData& table, Buffer output, int32_t wave_index_increment) {
	WavetableCycleChunker chunker{table, output, kPlayRate, /*wave_index=*/0, wave_index_increment};
	std::vector<CycleChunk> chunks;
	while (std::optional<CycleChunk> chunk = chunker.next()) {
		chunks.push_back(*chunk);
	}
	return chunks;
}
} // namespace

// clang-format off
describe wavetable_cycle_chunker("deluge::dsp::fixed WavetableCycleChunker", ${
	it("yields one whole-block single-cycle chunk for a single-cycle wavetable", _ {
		std::array<int32_t, kBlock> buffer{};
		WaveTableData table = make_table(/*num_cycles=*/1, /*transitions_magnitude=*/0, /*wave_index_multiplier=*/0x40000000);
		std::vector<CycleChunk> chunks = collect(table, Buffer{buffer}, /*wave_index_increment=*/0);
		expect(chunks.size()).to_equal(static_cast<std::size_t>(1));
		expect(chunks[0].single_cycle).to_be_truthy();
		expect(chunks[0].band != nullptr).to_be_truthy();
		expect(static_cast<int32_t>(chunks[0].output.size())).to_equal(kBlock);
	});

	it("yields one whole-block chunk for a static (non-sweeping) morph", _ {
		std::array<int32_t, kBlock> buffer{};
		WaveTableData table = make_table(/*num_cycles=*/4, /*transitions_magnitude=*/2, /*wave_index_multiplier=*/0x40000000);
		std::vector<CycleChunk> chunks = collect(table, Buffer{buffer}, /*wave_index_increment=*/0);
		expect(chunks.size()).to_equal(static_cast<std::size_t>(1));
		expect(!chunks[0].single_cycle).to_be_truthy();
		expect(chunks[0].first_cycle).to_equal(0);
		expect(static_cast<int32_t>(chunks[0].output.size())).to_equal(kBlock);
	});

	it("tiles the block into ascending-cycle chunks for a sweeping morph", _ {
		std::array<int32_t, kBlock> buffer{};
		WaveTableData table = make_table(/*num_cycles=*/4, /*transitions_magnitude=*/2, /*wave_index_multiplier=*/0x40000000);
		// A wave-index sweep that crosses a couple of cycle boundaries within the block.
		std::vector<CycleChunk> chunks = collect(table, Buffer{buffer}, /*wave_index_increment=*/0x02000000);
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
