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

// Drives the LFO and each individual lfo_wave generator as freestanding building blocks — no Sound/Voice/
// ParamManager/engine context. The exact-value cases are ported verbatim from the retired x86-only CppUTest
// suite (tests/unit/lfo_tests.cpp); freezing the same q31 outputs is the bit-exact proof that the
// std::variant rewrite preserves behaviour. Runs on x86 (SIMDe) and ARM/QEMU via CTest.

#include "dsp-next/fixed/bandlimited/waves.h" // triangle_full (the LFO's triangle shape)
#include "dsp-next/fixed/osc/sine_table.h"    // sine_table (the LFO's sine shape)
#include "synth/lfo.h"
#include "util/waves.h" // the jcong RNG state (for deterministic S&H / random-walk / warbler)
#include <cppspec.hpp>
#include <cstdint>

namespace {
// The retired suite reset CONG before each test for determinism; CONG == (jcong = …), so seeding jcong is
// equivalent. Call this at the start of any case that exercises randomness.
void seedRng() {
	jcong = 13287131;
}
} // namespace

// clang-format off
describe lfo_spec("LFO", ${
	// ---- Deterministic waveforms: exact q31 outputs ----
	it("renders a global triangle from zero", _ {
		LFO lfo;
		LFOConfig conf(LFOType::TRIANGLE);
		lfo.setGlobalInitialPhase(conf);
		expect(lfo.render(10, conf, 100)).to_equal(0);
		expect(lfo.render(0, conf, 0)).to_equal(10 * 100 * 2); // slope of 2 per phase step
	});

	it("renders a local triangle from the negative extreme", _ {
		LFO lfo;
		LFOConfig conf(LFOType::TRIANGLE);
		lfo.setLocalInitialPhase(conf);
		expect(lfo.render(10, conf, 100)).to_equal(INT32_MIN);
		expect(lfo.render(0, conf, 0)).to_equal(INT32_MIN + 2000);
	});

	it("renders a global sine from zero", _ {
		LFO lfo;
		LFOConfig conf(LFOType::SINE);
		lfo.setGlobalInitialPhase(conf);
		expect((int32_t)lfo.phase()).to_equal(0); // sin(0) == 0
		lfo.setPhase(1024);
		// (2^31)*sin(2*pi*1024/(2^32)) == 3216.99
		expect(lfo.render(0, conf, 0)).to_equal(3216);
	});

	it("renders a local sine from the negative extreme", _ {
		LFO lfo;
		LFOConfig conf(LFOType::SINE);
		lfo.setLocalInitialPhase(conf);
		expect(lfo.phase()).to_equal(3221225472u);
		expect(lfo.render(10, conf, 100)).to_equal(-2147418112);
		expect(lfo.render(0, conf, 0)).to_equal(-2147418082);
	});

	it("renders a saw with the same initial phase for local and global", _ {
		LFO lfo;
		LFOConfig conf(LFOType::SAW);
		lfo.setLocalInitialPhase(conf);
		uint32_t local_phase = lfo.phase();
		lfo.setGlobalInitialPhase(conf);
		expect(lfo.phase()).to_equal(local_phase);
		expect(lfo.render(10, conf, 100)).to_equal(INT32_MIN);
		expect(lfo.render(0, conf, 0)).to_equal(INT32_MIN + 1000);
	});

	it("renders a square and crosses its phase-width threshold", _ {
		LFO lfo;
		LFOConfig conf(LFOType::SQUARE);
		lfo.setLocalInitialPhase(conf);
		uint32_t local_phase = lfo.phase();
		lfo.setGlobalInitialPhase(conf);
		expect(lfo.phase()).to_equal(local_phase);
		expect(lfo.render(0, conf, 0)).to_equal(INT32_MAX); // historically the positive extreme
		lfo.setPhase(0x80000001);                           // force over the phase width
		expect(lfo.render(0, conf, 0)).to_equal(INT32_MIN);
	});

	// ---- Random waveforms: deterministic under a fixed jcong seed ----
	it("samples and holds a random value per period", _ {
		seedRng();
		LFO lfo;
		LFOConfig conf(LFOType::SAMPLE_AND_HOLD);
		lfo.setLocalInitialPhase(conf);
		uint32_t local_phase = lfo.phase();
		conf.syncLevel = SYNC_LEVEL_8TH;
		lfo.setGlobalInitialPhase(conf);
		expect(lfo.phase()).to_equal(local_phase);
		expect(lfo.phase()).to_equal(0u);

		int32_t value = lfo.render(10, conf, 10);
		expect(value).to_equal(-1392915738);
		expect(lfo.phase()).to_equal(100u);

		value = lfo.render(10, conf, 10); // held — no change
		expect(value).to_equal(-1392915738);
		expect(lfo.phase()).to_equal(200u);

		lfo.setPhase(0); // force a new period
		value = lfo.render(10, conf, 10);
		expect(value).to_equal(-28442955);
		expect(lfo.phase()).to_equal(100u);

		lfo.setPhase(UINT32_MAX); // next advance overflows -> new period
		value = lfo.render(10, conf, 10);
		expect(value).to_equal(-1725170056);
		expect(lfo.phase()).to_equal(99u);
	});

	it("random-walks a biased value per period", _ {
		seedRng();
		LFO lfo;
		LFOConfig conf(LFOType::RANDOM_WALK);
		lfo.setLocalInitialPhase(conf);
		conf.syncLevel = SYNC_LEVEL_8TH;
		lfo.setGlobalInitialPhase(conf);
		expect(lfo.phase()).to_equal(0u);

		int32_t value = lfo.render(10, conf, 10);
		expect(value).to_equal(-2948644);
		expect(lfo.phase()).to_equal(100u);

		value = lfo.render(10, conf, 10); // held
		expect(value).to_equal(-2948644);

		lfo.setPhase(0); // force a new period
		value = lfo.render(10, conf, 10);
		expect(value).to_equal(-78931243);

		lfo.setPhase(UINT32_MAX); // overflow -> new period
		value = lfo.render(10, conf, 10);
		expect(value).to_equal(-174189095);
		expect(lfo.phase()).to_equal(99u);
	});

	it("warbles at double phase rate", _ {
		seedRng();
		LFO lfo;
		LFOConfig conf(LFOType::WARBLER);
		lfo.setLocalInitialPhase(conf);
		expect(lfo.phase()).to_equal(0u);
		lfo.render(10, conf, 100);
		// The warbler advances phase at 2x the supplied increment (100 * 10 * 2).
		expect(lfo.phase()).to_equal(2000u);
	});

	// ---- The individual generators in isolation (the payoff of the decomposition) ----
	it("advances phase through a stateless generator", _ {
		lfo_wave::Sine sine;
		uint32_t phase = 0;
		expect(sine.render(phase, 100, 10)).to_equal(deluge::dsp::fixed::sine_table.value(0));
		expect(phase).to_equal(1000u); // 100 * 10
	});

	it("keeps a sample-and-hold value until the next period", _ {
		seedRng();
		lfo_wave::SampleHold sh;
		uint32_t phase = 0;
		int32_t first = sh.render(phase, 10, 10); // phase==0 -> fresh sample
		int32_t held = sh.render(phase, 10, 10);  // mid-period -> same value
		expect(held).to_equal(first);
	});

	// ---- Wave LUT sanity (ported from the retired suite's WaveTest) ----
	it("computes the triangle wave around its turnover points", _ {
		using deluge::dsp::fixed::triangle_full;
		expect(triangle_full(UINT32_MAX)).to_equal(-2147483647);
		expect(triangle_full(0)).to_equal(-2147483648);
		expect(triangle_full(1073741824)).to_equal(0);
		expect(triangle_full(2147483648u)).to_equal(2147483647);
		expect(triangle_full(3221225472u)).to_equal(-1);
	});
});
// clang-format on

CPPSPEC_MAIN(lfo_spec);
