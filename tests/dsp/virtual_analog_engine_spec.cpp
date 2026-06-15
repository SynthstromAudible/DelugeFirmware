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

// Behavioural spec for the L1 deluge::synth::VirtualAnalogEngine, run on x86 (SIMDe) and ARM/QEMU via CTest.
// Bit-exact equivalence with the legacy renderOsc on real presets is the golden harness's job; here we pin that
// the engine composes the right dsp-next oscillator and applies the right standalone scale per wave. We drive it
// with apply_amplitude=false (the raw "standalone" wave) and cross-check against the dsp-next kernel built
// directly. cpu_direness is set high to force the crude (non-band-limited) arms for deterministic comparison.

#include "synth/source/virtual_analog_engine.h"

#include "dsp-next/core/phasor.h"
#include "dsp-next/fixed/bandlimited/band_limited_table.h"
#include "dsp-next/fixed/osc/band_limited_table_oscillator.h"
#include "dsp-next/fixed/osc/crude_oscillators.h"
#include "dsp-next/fixed/sync/synced_phasor.h"
#include "util/lookuptables/lookuptables.h" // sineWaveSmall
#include <array>
#include <cppspec.hpp>
#include <cstdint>

namespace {
using deluge::dsp::Phasor;
using deluge::dsp::Ramp;
using deluge::synth::BlockControls;
using deluge::synth::VirtualAnalogEngine;
using Buffer = deluge::dsp::fixed::Buffer;

constexpr std::size_t kN = 8;

// Standalone controls (apply_amplitude=false): the engine writes the raw, scaled wave straight out.
BlockControls standalone(int32_t cpu_direness, uint32_t bandlimit_increment, uint32_t pulse_width = 2147483648u,
                         std::optional<Phasor> sync = std::nullopt) {
	return BlockControls{.amplitude = Ramp{0, 0},
	                     .apply_amplitude = false,
	                     .pulse_width = pulse_width,
	                     .pulse_wave = false,
	                     .retrigger_phase = 0,
	                     .sync = sync,
	                     .bandlimit_increment = bandlimit_increment,
	                     .cpu_direness = cpu_direness};
}

template <typename Osc>
std::array<int32_t, kN> renderKernel(Osc osc) {
	std::array<int32_t, kN> out{};
	osc.renderBlock(Buffer{out});
	return out;
}

std::array<int32_t, kN> renderEngine(OscType wave, uint32_t phase, uint32_t increment, const BlockControls& controls) {
	VirtualAnalogEngine engine{wave, Phasor{phase, increment}};
	std::array<int32_t, kN> out{};
	std::array<int32_t, kN> scratch{};
	engine.renderBlock(controls, Buffer{out}, Buffer{scratch});
	return out;
}
} // namespace

// clang-format off
describe vae_spec("deluge::synth::VirtualAnalogEngine", ${
	constexpr uint32_t kPhase = 0x12340000u;
	constexpr uint32_t kInc = 0x02000000u; // low pitch ⇒ low table number ⇒ crude arms (with cpu_direness 7)

	it("crude SAW is the phase ramp halved (standalone)", _ {
		auto got = renderEngine(OscType::SAW, kPhase, kInc, standalone(7, kInc));
		auto raw = renderKernel(deluge::dsp::fixed::CrudeSaw{Phasor{kPhase, kInc}});
		for (std::size_t i = 0; i < kN; ++i) {
			expect(got[i]).to_equal(raw[i] >> 1); // Standalone::kHalve
		}
	});

	it("crude SQUARE is the square halved (standalone)", _ {
		auto got = renderEngine(OscType::SQUARE, kPhase, kInc, standalone(7, kInc));
		auto raw = renderKernel(deluge::dsp::fixed::CrudeSquare{Phasor{kPhase, kInc}, 2147483648u});
		for (std::size_t i = 0; i < kN; ++i) {
			expect(got[i]).to_equal(raw[i] >> 1); // Standalone::kHalve
		}
	});

	it("crude TRIANGLE is the triangle doubled (standalone)", _ {
		auto got = renderEngine(OscType::TRIANGLE, kPhase, kInc, standalone(7, kInc));
		auto raw = renderKernel(deluge::dsp::fixed::CrudeTriangle{Phasor{kPhase, kInc}});
		for (std::size_t i = 0; i < kN; ++i) {
			expect(got[i]).to_equal(raw[i] << 1); // Standalone::kDouble
		}
	});

	it("SINE reads the sine band directly (no scale)", _ {
		static const deluge::dsp::fixed::Band kSineBand[] = {{sineWaveSmall, 8, 0xFFFFFFFFu}};
		static const deluge::dsp::fixed::BandlimitedTable kSineTable{kSineBand};
		auto got = renderEngine(OscType::SINE, kPhase, kInc, standalone(7, kInc));
		auto raw = renderKernel(deluge::dsp::fixed::BandLimitedTableOscillator{kSineTable, kPhase, kInc, kInc});
		for (std::size_t i = 0; i < kN; ++i) {
			expect(got[i]).to_equal(raw[i]); // Standalone::kDirect
		}
	});

	it("hard-synced crude SAW matches the synced kernel and reports its phase", _ {
		constexpr uint32_t kResetPhase = 0x40000000u;
		constexpr uint32_t kResetInc = 0x01000000u;
		auto controls = standalone(7, kInc, 2147483648u, Phasor{kResetPhase, kResetInc});

		VirtualAnalogEngine engine{OscType::SAW, Phasor{kPhase, kInc}};
		std::array<int32_t, kN> out{};
		std::array<int32_t, kN> scratch{};
		engine.renderBlock(controls, Buffer{out}, Buffer{scratch});

		deluge::dsp::fixed::CrudeSaw ref{deluge::dsp::fixed::SyncedPhasor{
		    Phasor{kPhase, kInc}, Phasor{kResetPhase, kResetInc}, 0u}};
		std::array<int32_t, kN> raw{};
		ref.renderBlock(Buffer{raw}); // render in place so ref.phase() reflects the advanced phase
		for (std::size_t i = 0; i < kN; ++i) {
			expect(out[i]).to_equal(raw[i] >> 1);
		}
		expect(engine.phase()).to_equal(ref.phase());
	});
});
// clang-format on

CPPSPEC_MAIN(vae_spec);
