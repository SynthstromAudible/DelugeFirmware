/*
 * Copyright © 2026 Mark Adams
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

// Pins the supersaw kernel against a plain per-sample polyBLEP in double precision, its block-size independence (the
// edge-to-edge BLEP has to carry an edge across a block boundary), its headroom, and the SupersawSource seam over it.
// Run on x86 (SIMDe) + ARM/QEMU via CTest.

#include "dsp-next/core/ramp.h"
#include "dsp-next/fixed/osc/supersaw.h"
#include "synth/source/supersaw_source.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cppspec.hpp>
#include <cstdint>
#include <numbers>
#include <vector>

namespace {
using deluge::dsp::Ramp;
using deluge::dsp::fixed::Buffer;
using deluge::dsp::fixed::Supersaw;
using deluge::synth::BlockControls;
using deluge::synth::SupersawSource;
namespace internal = deluge::dsp::fixed::supersaw_internal;

constexpr double kSampleRate = 44100;
constexpr uint32_t kStartPhase = 0x12345678;
constexpr int32_t kSpreadMin = -(1 << 30);
constexpr int32_t kSpreadMax = (1 << 30) - 1;

uint32_t incrementFor(double hz) {
	return static_cast<uint32_t>(hz / kSampleRate * 4294967296.0);
}

/// Render @p total samples in blocks cycling through @p blocks, from the same starting phases each time
std::vector<int32_t> render(uint32_t inc, int32_t spread, int32_t saws, int32_t total, std::vector<int32_t> blocks,
                            Supersaw::SidePhases sides = Supersaw::sidePhasesFrom(kStartPhase),
                            uint32_t centre = kStartPhase) {
	std::vector<int32_t> out(total);
	std::vector<int32_t> block(256 + 4);
	for (int32_t pos = 0, b = 0; pos < total; b++) {
		int32_t n = std::min(blocks[b % blocks.size()], total - pos);
		Supersaw::render(Buffer{block.data(), static_cast<size_t>(n)}, inc, spread, saws, centre, sides, true);
		std::copy_n(block.begin(), n, out.begin() + pos);
		pos += n;
	}
	return out;
}

/// The same supersaw as a straightforward per-sample polyBLEP in double precision, with the kernel's increments and
/// gains
std::vector<double> reference(uint32_t inc, int32_t spread, int32_t saws, int32_t total,
                              Supersaw::SidePhases sides = Supersaw::sidePhasesFrom(kStartPhase),
                              uint32_t centre = kStartPhase) {
	float x =
	    static_cast<float>(std::clamp(spread, kSpreadMin, kSpreadMax) + (1 << 30)) * (internal::kMaxSpread * 0x1p-31f);
	float detune = x * x;
	double gc = internal::centreMix(x);
	double gs = internal::sideMix(x);
	double norm = 1 / std::sqrt(gc * gc + (saws - 1) * gs * gs);
	gc *= norm;
	gs *= norm;

	std::array<uint32_t, Supersaw::kNumSaws> dts{};
	std::array<uint32_t, Supersaw::kNumSaws> phases{centre};
	for (size_t i = 0; i < Supersaw::kNumSaws; i++) {
		dts[i] = i == 0 ? inc
		                : static_cast<uint32_t>((1.f + internal::kDetuneOffsets[i] * detune) * static_cast<float>(inc));
		if (i > 0) {
			phases[i] = sides[i - 1];
		}
	}

	std::vector<double> out(total);
	for (int32_t n = 0; n < total; n++) {
		double y = 0;
		for (int32_t i = 0; i < saws; i++) {
			phases[i] += dts[i];
			double dt = dts[i] / 4294967296.0;
			double p = static_cast<uint32_t>(phases[i] ^ 0x80000000u) / 4294967296.0; // 0 at the falling edge
			double saw = static_cast<int32_t>(phases[i]) / 2147483648.0;
			if (p < dt) {
				double t = p / dt;
				saw += (1 - t) * (1 - t);
			}
			else if (p > 1 - dt) {
				double t = (p - 1) / dt;
				saw -= (1 + t) * (1 + t);
			}
			y += saw * (i == 0 ? gc : gs);
		}
		out[n] = y * (1 << 29);
	}
	return out;
}

double maxError(const std::vector<int32_t>& got, const std::vector<double>& want) {
	double err = 0;
	for (size_t i = 0; i < got.size(); i++) {
		err = std::max(err, std::abs(got[i] - want[i]));
	}
	return err;
}

/// Power far from any harmonic of the note, over power near one, in dB. At minimum spread every saw's harmonics sit
/// near the note's
double aliasRatioDb(const std::vector<int32_t>& x, double hz) {
	const int32_t n = static_cast<int32_t>(x.size());
	double signal = 0;
	double alias = 0;
	for (int32_t k = 1; k < n / 2; k++) {
		std::complex<double> acc = 0;
		for (int32_t i = 0; i < n; i++) {
			double window = 0.5 - 0.5 * std::cos(2 * std::numbers::pi * i / n);
			acc += window * x[i] * std::polar(1.0, -2 * std::numbers::pi * k * i / n);
		}
		double harmonic = k * kSampleRate / n / hz;
		(std::abs(harmonic - std::round(harmonic)) < 0.15 ? signal : alias) += std::norm(acc);
	}
	return 10 * std::log10(alias / signal);
}

BlockControls controls(bool applyAmplitude, Ramp amplitude = Ramp{0, 0}) {
	return BlockControls{.amplitude = amplitude,
	                     .apply_amplitude = applyAmplitude,
	                     .pulse_width = 0,
	                     .retrigger_phase = 0,
	                     .sync = std::nullopt,
	                     .cpu_direness = 0};
}
} // namespace

// clang-format off
describe supersaw("the dsp-next supersaw", ${
	context("the kernel", _ {
		it("matches a per-sample polyBLEP for every saw count, spread and pitch", _ {
			for (double hz : {55.0, 440.0, 1760.0, 5000.0}) {
				for (int32_t saws = 1; saws <= static_cast<int32_t>(Supersaw::kNumSaws); saws++) {
					for (int32_t spread : {kSpreadMin, -(1 << 29), 0, 1 << 29, kSpreadMax}) {
						auto got = render(incrementFor(hz), spread, saws, 2048, {128});
						auto want = reference(incrementFor(hz), spread, saws, 2048);
						// Float rounding in the edge maths, well under a part in a million of full scale
						expect(maxError(got, want)).to_be_less_than(1e-6 * (1 << 29));
					}
				}
			}
		});

		it("gives the same output whatever the block sizes, including edges that land on a block boundary", _ {
			for (double hz : {440.0, 5000.0}) {
				for (int32_t saws : {1, 4, 7}) {
					auto whole = render(incrementFor(hz), 0, saws, 4096, {128});
					auto odd = render(incrementFor(hz), 0, saws, 4096, {4, 36, 128, 20, 8, 100});
					expect(odd == whole).to_be_true();
				}
			}
		});

		it("doesn't overflow with every saw starting in phase at full spread", _ {
			Supersaw::SidePhases inPhase{};
			for (int32_t saws = 2; saws <= static_cast<int32_t>(Supersaw::kNumSaws); saws++) {
				auto got = render(incrementFor(440), kSpreadMax, saws, 1024, {128}, inPhase, 0);
				auto want = reference(incrementFor(440), kSpreadMax, saws, 1024, inPhase, 0);
				expect(maxError(got, want)).to_be_less_than(1e-6 * (1 << 29));
			}
		});

		it("cuts the aliasing of a naive supersaw by over 40dB at 1760Hz", _ {
			const uint32_t inc = incrementFor(1760);
			std::vector<int32_t> naive(2048);
			uint32_t centre = kStartPhase;
			auto sides = Supersaw::sidePhasesFrom(kStartPhase);
			for (int32_t pos = 0; pos < 2048; pos += 128) {
				Supersaw::render(Buffer{naive.data() + pos, 128}, inc, kSpreadMin, 7, centre, sides, false);
			}
			auto blep = render(inc, kSpreadMin, 7, 2048, {128});
			expect(aliasRatioDb(blep, 1760)).to_be_less_than(aliasRatioDb(naive, 1760) - 40);
		});

		it("takes saws away as pulse width goes up, either way round", _ {
			expect(Supersaw::numSawsFromPulseWidth(0)).to_equal(7);
			expect(Supersaw::numSawsFromPulseWidth(1u << 29)).to_equal(6);
			expect(Supersaw::numSawsFromPulseWidth(1u << 30)).to_equal(4);
			expect(Supersaw::numSawsFromPulseWidth(0x7FFFFFFFu)).to_equal(1);
			expect(Supersaw::numSawsFromPulseWidth(0x80000000u)).to_equal(1);
			expect(Supersaw::numSawsFromPulseWidth(static_cast<uint32_t>(-(1 << 30)))).to_equal(4);
		});
	});

	context("SupersawSource", _ {
		it("renders the raw kernel standalone, keeping its phases across blocks", _ {
			const uint32_t inc = incrementFor(440);
			SupersawSource source{kStartPhase, Supersaw::sidePhasesFrom(kStartPhase)};
			std::vector<int32_t> got(1024);
			std::vector<int32_t> scratch(128);
			for (int32_t pos = 0; pos < 1024; pos += 128) {
				source.renderInto(controls(false), inc, 0, 0, Buffer{got.data() + pos, 128}, Buffer{scratch});
			}
			expect(got == render(inc, 0, 7, 1024, {128})).to_be_true();
			expect(source.phase()).to_equal(kStartPhase + inc * 1024);
		});

		it("mixes in through the VCA at four times the amplitude, as a quarter-level wave", _ {
			const uint32_t inc = incrementFor(440);
			const Ramp amplitude{1 << 26, 0};
			SupersawSource raw{kStartPhase, Supersaw::sidePhasesFrom(kStartPhase)};
			SupersawSource mixed{kStartPhase, Supersaw::sidePhasesFrom(kStartPhase)};
			std::vector<int32_t> wave(128);
			std::vector<int32_t> out(128, 1000);
			std::vector<int32_t> scratch(128);
			raw.renderInto(controls(false), inc, 0, 0, Buffer{wave}, Buffer{scratch});
			mixed.renderInto(controls(true, amplitude), inc, 0, 0, Buffer{out}, Buffer{scratch});
			for (size_t i = 0; i < out.size(); i++) {
				// vqdmulh by the halved, quadrupled amplitude, plus what was already in the buffer
				auto want = static_cast<int32_t>((static_cast<int64_t>(wave[i]) * ((amplitude.value() << 2) >> 1)) >> 31) + 1000;
				expect(out[i]).to_equal(want);
			}
		});

		it("starts every retriggered note the same way", _ {
			const uint32_t inc = incrementFor(440);
			std::vector<int32_t> first(128);
			std::vector<int32_t> second(128);
			std::vector<int32_t> scratch(128);
			SupersawSource a{0, Supersaw::sidePhasesFrom(0)};
			SupersawSource b{0, Supersaw::sidePhasesFrom(0)};
			a.renderInto(controls(false), inc, 0, 0, Buffer{first}, Buffer{scratch});
			b.renderInto(controls(false), inc, 0, 0, Buffer{second}, Buffer{scratch});
			expect(first == second).to_be_true();
		});
	});
});

CPPSPEC_MAIN(supersaw);
