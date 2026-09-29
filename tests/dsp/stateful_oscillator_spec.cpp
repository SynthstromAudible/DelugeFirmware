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

// Pins the *stateful* contract the dsp-next oscillators gained: phase persists across blocks, and per-block
// param updates (setIncrement / setTable) are bit-identical to constructing a fresh oscillator. This is the
// foundation the persistent voice model relies on; the band-reselect-on-setIncrement case is the key hazard.
// Run on x86 (SIMDe) + ARM/QEMU via CTest.

#include "dsp-next/fixed/bandlimited/saw.h"
#include "dsp-next/fixed/bandlimited/square.h"
#include "dsp-next/fixed/osc/band_limited_table_oscillator.h"
#include "dsp-next/fixed/osc/crude_oscillators.h"
#include "dsp-next/fixed/osc/pulse_oscillator.h"
#include "dsp-next/fixed/osc/table_oscillator.h"
#include <array>
#include <cppspec.hpp>
#include <cstdint>

namespace {
using deluge::dsp::Phasor;
using deluge::dsp::fixed::BandLimitedTableOscillator;
using deluge::dsp::fixed::CrudeSaw;
using deluge::dsp::fixed::PulseOscillator;
using deluge::dsp::fixed::saw_bandlimited_table;
using deluge::dsp::fixed::square_bandlimited_table;
using deluge::dsp::fixed::Table;
using deluge::dsp::fixed::TableOscillator;
using Buffer = deluge::dsp::fixed::Buffer;

constexpr std::size_t kN = 16; // a render block

template <typename Osc>
std::array<int32_t, kN> block(Osc& osc) {
	std::array<int32_t, kN> out{};
	osc.renderBlock(Buffer{out});
	return out;
}
} // namespace

// clang-format off
describe stateful_oscillator("dsp-next stateful oscillators", ${
	constexpr int32_t kMag = 2;
	constexpr uint32_t kInc = 0x08000000u;
	constexpr uint32_t kInc2 = 0x01000000u;

	context("TableOscillator", _ {
		it("persisting across two blocks equals reseeding a fresh oscillator", _ {
			std::array<int16_t, 8> table{0, 1000, 2000, 3000, 4000, 5000, 6000, 7000};
			TableOscillator persistent{Table{table, kMag}, 0u, kInc};
			block(persistent);                       // block 1
			auto cont = block(persistent);           // block 2 (continues)

			TableOscillator first{Table{table, kMag}, 0u, kInc};
			block(first);
			TableOscillator reseeded{Table{table, kMag}, first.phase(), kInc};
			expect(cont).to_equal(block(reseeded));
		});

		it("setIncrement then render equals constructing fresh at the new increment + phase", _ {
			std::array<int16_t, 8> table{0, 1000, 2000, 3000, 4000, 5000, 6000, 7000};
			TableOscillator a{Table{table, kMag}, 0u, kInc};
			block(a);
			a.setIncrement(kInc2);
			auto got = block(a);

			TableOscillator b{Table{table, kMag}, 0u, kInc};
			block(b);
			TableOscillator fresh{Table{table, kMag}, b.phase(), kInc2};
			expect(got).to_equal(block(fresh));
		});

		it("setTable swaps the band, keeping the phase", _ {
			std::array<int16_t, 8> tableA{0, 1000, 2000, 3000, 4000, 5000, 6000, 7000};
			std::array<int16_t, 8> tableB{7000, 6000, 5000, 4000, 3000, 2000, 1000, 0};
			TableOscillator a{Table{tableA, kMag}, 0u, kInc};
			block(a);
			a.setTable(Table{tableB, kMag});
			auto got = block(a);

			TableOscillator b{Table{tableA, kMag}, 0u, kInc};
			block(b);
			TableOscillator fresh{Table{tableB, kMag}, b.phase(), kInc};
			expect(got).to_equal(block(fresh));
		});
	});

	// The primary hazard: a band-limited oscillator must re-select its band when the pitch changes, and that must
	// be bit-identical to constructing fresh at the new pitch. Two widely different increments pick different bands.
	context("BandLimitedTableOscillator", _ {
		it("setIncrement re-selects the band, equalling a fresh oscillator at the new pitch", _ {
			constexpr uint32_t kLow = 0x00200000u;
			constexpr uint32_t kHigh = 0x30000000u;
			BandLimitedTableOscillator a{saw_bandlimited_table(), 0u, kLow};
			block(a);
			a.setIncrement(kHigh);
			auto got = block(a);

			BandLimitedTableOscillator b{saw_bandlimited_table(), 0u, kLow};
			block(b);
			BandLimitedTableOscillator fresh{saw_bandlimited_table(), b.phase(), kHigh};
			expect(got).to_equal(block(fresh));
		});
	});

	context("PulseOscillator", _ {
		it("persisting across two blocks equals reseeding a fresh oscillator", _ {
			Table band = square_bandlimited_table().select(kInc);
			constexpr uint32_t kOffset = 0x40000000u;
			PulseOscillator persistent{band, 0u, kInc, kOffset};
			block(persistent);
			auto cont = block(persistent);

			PulseOscillator first{band, 0u, kInc, kOffset};
			block(first);
			PulseOscillator reseeded{band, first.phase(), kInc, kOffset};
			expect(cont).to_equal(block(reseeded));
		});
	});

	context("CrudeSaw", _ {
		it("setIncrement then render equals a fresh oscillator at the new increment + phase", _ {
			CrudeSaw a{Phasor{0u, kInc}};
			block(a);
			a.setIncrement(kInc2);
			auto got = block(a);

			CrudeSaw b{Phasor{0u, kInc}};
			block(b);
			CrudeSaw fresh{Phasor{b.phase(), kInc2}};
			expect(got).to_equal(block(fresh));
		});
	});
});
// clang-format on

CPPSPEC_MAIN(stateful_oscillator);
