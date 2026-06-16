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
#include "dsp-next/fixed/bandlimited/analog_saw.h"
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
using Wave = deluge::synth::va_shape::Wave;
using Buffer = deluge::dsp::fixed::Buffer;

constexpr std::size_t kN = 8;

// Standalone controls (apply_amplitude=false): the engine writes the raw, scaled wave straight out. pulse_width
// is the raw modulation value (0 ⇒ no pulse); the shapes derive pulse/sync/retrigger/band-select themselves.
BlockControls standalone(int32_t cpu_direness, uint32_t pulse_width = 0, std::optional<Phasor> sync = std::nullopt) {
	return BlockControls{.amplitude = Ramp{0, 0},
	                     .apply_amplitude = false,
	                     .pulse_width = pulse_width,
	                     .retrigger_phase = 0,
	                     .sync = sync,
	                     .cpu_direness = cpu_direness};
}

template <typename Osc>
std::array<int32_t, kN> renderKernel(Osc osc) {
	std::array<int32_t, kN> out{};
	osc.renderBlock(Buffer{out});
	return out;
}

std::array<int32_t, kN> renderEngine(Wave wave, uint32_t phase, uint32_t increment, const BlockControls& controls) {
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
		auto got = renderEngine(Wave::Saw, kPhase, kInc, standalone(7));
		auto raw = renderKernel(deluge::dsp::fixed::CrudeSaw{Phasor{kPhase, kInc}});
		for (std::size_t i = 0; i < kN; ++i) {
			expect(got[i]).to_equal(raw[i] >> 1); // Standalone::kHalve
		}
	});

	it("crude SQUARE is the square halved (standalone)", _ {
		auto got = renderEngine(Wave::Square, kPhase, kInc, standalone(7));
		auto raw = renderKernel(deluge::dsp::fixed::CrudeSquare{Phasor{kPhase, kInc}, 2147483648u});
		for (std::size_t i = 0; i < kN; ++i) {
			expect(got[i]).to_equal(raw[i] >> 1); // Standalone::kHalve
		}
	});

	it("crude TRIANGLE is the triangle doubled (standalone)", _ {
		auto got = renderEngine(Wave::Triangle, kPhase, kInc, standalone(7));
		auto raw = renderKernel(deluge::dsp::fixed::CrudeTriangle{Phasor{kPhase, kInc}});
		for (std::size_t i = 0; i < kN; ++i) {
			expect(got[i]).to_equal(raw[i] << 1); // Standalone::kDouble
		}
	});

	it("SINE reads the sine band directly (no scale)", _ {
		static const deluge::dsp::fixed::Band kSineBand[] = {{sineWaveSmall, 8, 0xFFFFFFFFu}};
		static const deluge::dsp::fixed::BandlimitedTable kSineTable{kSineBand};
		auto got = renderEngine(Wave::Sine, kPhase, kInc, standalone(7));
		auto raw = renderKernel(deluge::dsp::fixed::BandLimitedTableOscillator{kSineTable, kPhase, kInc, kInc});
		for (std::size_t i = 0; i < kN; ++i) {
			expect(got[i]).to_equal(raw[i]); // Standalone::kDirect
		}
	});

	// kInc ⇒ table number 10. With dire CPU (10 < 7+6) the analog saw downgrades — a decision the AnalogSaw
	// shape now owns internally — to the crude *digital* saw (kHalve, no retrigger tweak); with fine CPU it
	// stays band-limited.
	it("analog SAW downgrades to the crude digital saw at dire CPU", _ {
		auto got = renderEngine(Wave::AnalogSaw, kPhase, kInc, standalone(7));
		auto raw = renderKernel(deluge::dsp::fixed::CrudeSaw{Phasor{kPhase, kInc}});
		for (std::size_t i = 0; i < kN; ++i) {
			expect(got[i]).to_equal(raw[i] >> 1); // Standalone::kHalve
		}
	});

	it("analog SAW stays band-limited when CPU is fine", _ {
		auto got = renderEngine(Wave::AnalogSaw, kPhase, kInc, standalone(0));
		auto raw = renderKernel(deluge::dsp::fixed::BandLimitedTableOscillator{
		    deluge::dsp::fixed::analog_saw_bandlimited_table(), kPhase, kInc, kInc});
		for (std::size_t i = 0; i < kN; ++i) {
			expect(got[i]).to_equal(raw[i]); // Standalone::kDirect
		}
	});

	// Persistence: the engine owns its phase across blocks. With no real sync it advances the original-domain
	// phase by increment·numSamples per block, so a second block equals a fresh engine seeded at the advanced
	// phase — this is what lets Voice retire oscPos and hold the engine across render blocks.
	it("phase persists across blocks (== reseed each block)", _ {
		VirtualAnalogEngine persistent{Wave::Saw, Phasor{kPhase, kInc}};
		std::array<int32_t, kN> out1{};
		std::array<int32_t, kN> scratch1{};
		persistent.renderBlock(standalone(7), Buffer{out1}, Buffer{scratch1});
		expect(persistent.phase()).to_equal(kPhase + kInc * kN); // original-domain self-advance

		std::array<int32_t, kN> out2{};
		std::array<int32_t, kN> scratch2{};
		persistent.renderBlock(standalone(7), Buffer{out2}, Buffer{scratch2});

		auto fresh = renderEngine(Wave::Saw, kPhase + kInc * kN, kInc, standalone(7));
		for (std::size_t i = 0; i < kN; ++i) {
			expect(out2[i]).to_equal(fresh[i]);
		}
	});

	it("setIncrement sets the per-block pitch (and the self-advance follows it)", _ {
		VirtualAnalogEngine engine{Wave::Saw, Phasor{kPhase, 0}};
		engine.setIncrement(kInc);
		expect(engine.increment()).to_equal(kInc);
		std::array<int32_t, kN> out{};
		std::array<int32_t, kN> scratch{};
		engine.renderBlock(standalone(7), Buffer{out}, Buffer{scratch});
		expect(engine.phase()).to_equal(kPhase + kInc * kN);
	});

	it("hard-synced crude SAW matches the synced kernel and reports its phase", _ {
		constexpr uint32_t kResetPhase = 0x40000000u;
		constexpr uint32_t kResetInc = 0x01000000u;
		auto controls = standalone(7, 0, Phasor{kResetPhase, kResetInc});

		VirtualAnalogEngine engine{Wave::Saw, Phasor{kPhase, kInc}};
		std::array<int32_t, kN> out{};
		std::array<int32_t, kN> scratch{};
		engine.renderBlock(controls, Buffer{out}, Buffer{scratch});

		// The Saw shape applies the +2^31 retrigger tweak internally, so the synced kernel must use it too.
		deluge::dsp::fixed::CrudeSaw ref{deluge::dsp::fixed::SyncedPhasor{
		    Phasor{kPhase, kInc}, Phasor{kResetPhase, kResetInc}, 2147483648u}};
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
