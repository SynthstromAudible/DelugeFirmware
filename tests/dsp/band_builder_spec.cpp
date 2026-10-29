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

// fixed::BandBuilder against the recorded tables: every pinned wavetable's bands built from its file, cycle by cycle,
// as the loader builds them, and pinned by CRC.

#include "dsp-next/fixed/fft/real_fft.h"
#include "dsp-next/fixed/fft/twiddles.h"
#include "dsp-next/fixed/wavetable/band_builder.h"
#include "dsp-next/fixed/wavetable/wave_table_data.h"
#include "wavetable_band_crc.h"
#include "wavetable_band_records.h"
#include "wavetable_fixture.h"
#include <algorithm>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace {

using namespace deluge::dsp::fixed;

/// The transform of 2^@p m points, built as the app builds it and kept for the run.
const fft::RealFft& transform(int32_t m) {
	struct Built {
		std::vector<fft::Complex> block;
		std::optional<fft::RealFft> fft;
	};
	static std::map<int32_t, Built> built;
	auto [entry, inserted] = built.try_emplace(m);
	if (inserted) {
		Built& b = entry->second;
		b.block.resize(fft::storage_bytes(m) / sizeof(fft::Complex));
		const fft::Layout layout = fft::lay_out(std::as_writable_bytes(std::span{b.block}), m);
		fft::Twiddles twiddles = layout.twiddles;
		fft::factor(twiddles);
		fft::generate_stage_twiddles(twiddles);
		fft::generate_super_twiddles(twiddles);
		b.fft.emplace(twiddles, layout.scratch);
	}
	return *entry->second.fft;
}

/// A wavetable built from a fixture's samples as WaveTable::setup builds it, every band's transform available.
struct Table {
	int32_t num_cycles = 0;
	std::vector<std::vector<int16_t>> samples;
	std::vector<WaveTableBand> bands;

	explicit Table(const wavetable_fixture::Fixture& fixture) {
		const auto length = static_cast<int32_t>(fixture.samples.size());
		const int32_t cycle_size = std::min(fixture.cycle_size, length);
		const BandGeometry geometry = BandGeometry::of(cycle_size);
		num_cycles = geometry.source_is_power_of_two ? length >> geometry.band0_magnitude : 1;
		const int32_t num_bands = geometry.num_bands(true);

		for (int32_t b = 0; b < num_bands; ++b) {
			samples.emplace_back(
			    static_cast<std::size_t>(num_cycles * (geometry.cycle_size(b) + kWavetableDuplicateSamplesAtCycleEnd)));
			bands.push_back({
			    .maxPhaseIncrement = geometry.max_phase_increment(b),
			    .fromCycleNumber = 0,
			    .toCycleNumber = 0,
			    .cycleSizeNoDuplicates = static_cast<uint16_t>(geometry.cycle_size(b)),
			    .cycleSizeMagnitude = static_cast<uint8_t>(geometry.cycle_magnitude(b)),
			    .dataAccessAddress = samples.back().data(),
			});
		}
		const auto target = [&](int32_t b) {
			return BandTarget{.samples = samples[b].data(), .cycle_magnitude = geometry.cycle_magnitude(b)};
		};

		const int32_t working = std::max(cycle_size, geometry.cycle_size(0));
		std::vector<int32_t> cycle(static_cast<std::size_t>(working));
		std::vector<fft::Complex> spectrum(static_cast<std::size_t>((working >> 1) + 1));
		BandBuilder builder{
		    {.source_cycle_size = cycle_size, .band0_magnitude = geometry.band0_magnitude, .num_cycles = num_cycles},
		    cycle,
		    spectrum};

		for (int32_t c = 0; c < num_cycles; ++c) {
			const std::span<int32_t> input = builder.cycle_input();
			for (std::size_t i = 0; i < input.size(); ++i) {
				input[i] = fixture.samples[(static_cast<std::size_t>(c) * input.size()) + i] >> kCycleHeadroomBits;
			}
			int32_t first_rendered = 0;
			if (geometry.source_is_power_of_two) {
				builder.copy_band0(target(0), c);
				if (num_bands == 1) {
					bands[0].toCycleNumber = builder.to_cycle_after(c);
					continue;
				}
				builder.analyse(&transform(geometry.band0_magnitude));
				first_rendered = 1;
			}
			else {
				builder.analyse(nullptr);
			}
			bands[0].toCycleNumber = builder.to_cycle_after(c);
			for (int32_t b = first_rendered; b < num_bands; ++b) {
				bands[b].toCycleNumber = builder.to_cycle_after(c);
				builder.render_band(target(b), c, transform(geometry.cycle_magnitude(b)));
			}
		}
	}

	[[nodiscard]] WaveTableData view() const {
		return {num_cycles, 0, 0, bands.data(), sizeof(WaveTableBand), static_cast<int32_t>(bands.size())};
	}
};

} // namespace

// clang-format off
describe band_builder("fixed::BandBuilder", ${
	it("builds every pinned wavetable's bands as recorded", _ {
		for (const auto& record : wavetable_band_records::kRecords) {
			const auto fixture =
			    wavetable_fixture::load(wavetable_fixture::repo_root() / "tests/integration/golden" / record.file);
			expect(fixture.has_value()).to_equal(true);
			if (!fixture) {
				continue;
			}
			const Table table{*fixture};
			expect(static_cast<int32_t>(table.bands.size())).to_equal(record.num_bands);
			expect(wavetable_band_crc(table.view())).to_equal(record.crc);
		}
	});

	it("lays a 600-sample cycle out as eight bands from 1024 samples down to 8", _ {
		const BandGeometry geometry = BandGeometry::of(600);
		expect(geometry.band0_magnitude).to_equal(10);
		expect(geometry.source_is_power_of_two).to_equal(false);
		expect(geometry.num_bands(true)).to_equal(8);
		expect(geometry.cycle_size(7)).to_equal(8);
	});
});
// clang-format on

CPPSPEC_MAIN(band_builder);
