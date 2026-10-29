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
#include "supersaw.h"
#include <algorithm>
#include <argon.hpp>
#include <bit>
#include <cmath>

namespace deluge::dsp {
namespace {

constexpr double constexprSqrt(double v) {
	double r = std::max(v, 1.0);
	for (int32_t i = 0; i < 32; i++) {
		r = 0.5 * (r + v / r);
	}
	return r;
}

// Frequency offset of each saw at full detune (from Adam Szabo's measurements of a JP-8000, "How to Emulate the Super
// Saw", 2010), and a spare lane so the increments can be worked out as two vectors. Ordered as saws get added: the
// centre saw, then the outer pair, so that a few saws still spread as wide as seven, then the middle pair, then the
// inner pair
alignas(16) constexpr std::array<float, 8> kDetuneOffsets = {
    0.f, -0.11002313f, 0.10745242f, -0.06288439f, 0.06216538f, -0.01952356f, 0.01991221f, 0.f,
};

// Spread at the top of the wave index param, so wave 50 gives what spread 0.6 (wave 30) used to
constexpr float kMaxSpread = 0.6f;

// As spread goes up the centre saw fades down and the side saws come up (Szabo's mix curves), before normalising
constexpr float centreMix(float spread) {
	return 0.99785f - 0.55366f * spread;
}
constexpr float sideMix(float spread) {
	return (1.2841f - 0.73764f * spread) * spread + 0.044372f;
}

/// The side saws are summed shifted right by this, the least that can't overflow
constexpr int32_t sideShift(size_t numSaws) {
	return std::bit_width(numSaws - 2);
}

/// Gains normalised so the summed (uncorrelated) saws have the RMS of a single saw, so spread and saw count change
/// the sound without changing the level
constexpr std::pair<double, double> normalisedGains(float spread, size_t numSaws) {
	double centre = centreMix(spread);
	double side = sideMix(spread);
	double norm = 1.0 / constexprSqrt(centre * centre + (numSaws - 1) * side * side);
	return {centre * norm, side * norm};
}

// The side gain goes to Q(29 + shift) and has to fit, and the worst case sum of all saws has to fit in an int32
static_assert([] {
	for (int32_t i = 0; i <= 1024; i++) {
		for (size_t numSaws = 2; numSaws <= Supersaw::kNumSaws; numSaws++) {
			auto [centre, side] = normalisedGains(kMaxSpread * i / 1024.f, numSaws);
			if (side * (1u << sideShift(numSaws)) >= 4 || centre + (numSaws - 1) * side >= 4) {
				return false;
			}
		}
	}
	return true;
}());

/// The naive (aliasing) saws, mixed, 4 samples per vector. Templated on the number of saws so each count gets its own
/// fully unrolled loop with every phase in a register.
template <size_t kSaws>
[[gnu::hot]] void renderNaive(int32_t* __restrict__ buffer, int32_t numSamples, const uint32_t* phases,
                              const uint32_t* increments, float centreGain, float sideGain) {
	constexpr int32_t kShift = kSaws > 1 ? sideShift(kSaws) : 0;

	// Both gains in one register, multiplied in by lane, otherwise GCC moves them over from core registers every loop.
	// Centre saw is used at full scale, the side saws are summed at 1/2^kShift scale
	Argon<int32_t> gains{static_cast<int32_t>(centreGain * 0x1p29f),
	                     static_cast<int32_t>(sideGain * 0x1p29f * (1 << kShift)), 0, 0};

	std::array<Argon<uint32_t>, kSaws> phaseVectors;
	std::array<Argon<uint32_t>, kSaws> stepVectors;
#pragma GCC unroll 7
	for (size_t i = 0; i < kSaws; i++) {
		phaseVectors[i] = Argon<uint32_t>{phases[i]}.MultiplyAdd(uint32x4_t{1, 2, 3, 4}, increments[i]);
		stepVectors[i] = Argon<uint32_t>{increments[i] * 4};
	}

	int32_t* __restrict__ out = buffer;
	int32_t const* const end = buffer + numSamples;
	do {
		Argon<int32_t> mix = phaseVectors[0].template As<int32_t>().MultiplyFixedQMax(gains[0]);
		if constexpr (kSaws > 1) {
			Argon<int32_t> sides = phaseVectors[1].template As<int32_t>();
			if constexpr (kShift > 0) {
				sides = sides.ShiftRight<kShift>();
			}
			if constexpr (kSaws > 2) { // A shift of 0 isn't a valid vsra, even in a loop that never runs
#pragma GCC unroll 7
				for (size_t i = 2; i < kSaws; i++) {
					sides = sides.ShiftRightAccumulate<kShift>(phaseVectors[i].template As<int32_t>());
				}
			}
			mix = mix + sides.MultiplyFixedQMax(gains[1]);
		}
		mix.StoreTo(out);

#pragma GCC unroll 7
		for (size_t i = 0; i < kSaws; i++) {
			phaseVectors[i] = phaseVectors[i] + stepVectors[i];
		}
		out += 4;
	} while (out < end);
}

/// Add the polyBLEP residual around every falling edge of one saw. The saw is (int32_t)phase, which falls from max to
/// min where phase crosses 2^31. Edges just past the end of the buffer still correct its last sample. That edge comes
/// round again at the start of the next buffer, which then does the sample after it.
[[gnu::always_inline]] inline void addBlepResiduals(int32_t* __restrict__ buffer, int32_t numSamples, uint32_t phase,
                                                    uint32_t increment, float reciprocal, float scale) {
	// Phase measured from the falling edge
	uint32_t x = phase ^ 0x80000000u;
	int32_t sample = -1;
	while (true) {
		// The next edge is floor(y / increment) + 1 samples on. The float estimate is within 1 of that, the
		// remainder fixes it up exactly
		uint32_t y = ~x;
		float estimate = static_cast<float>(y) * reciprocal;
		if (estimate >= static_cast<float>(numSamples - sample + 1)) {
			return;
		}
		uint32_t quotient = static_cast<uint32_t>(estimate);
		int64_t remainder = static_cast<int64_t>(y) - static_cast<int64_t>(static_cast<uint64_t>(quotient) * increment);
		if (remainder < 0) {
			quotient--;
			remainder += increment;
		}
		else if (remainder >= increment) {
			quotient++;
			remainder -= increment;
		}

		sample += static_cast<int32_t>(quotient) + 1;
		if (sample > numSamples) {
			return;
		}

		// How far past the edge this sample is, in samples
		x = increment - 1 - static_cast<uint32_t>(remainder);
		float t = std::min(static_cast<float>(x) * reciprocal, 1.f);

		if (sample > 0) {
			buffer[sample - 1] -= static_cast<int32_t>(t * t * scale);
		}
		if (sample < numSamples) {
			float u = 1.f - t;
			buffer[sample] += static_cast<int32_t>(u * u * scale);
		}
	}
}

} // namespace

// Performance notes for render()
//
// Design: the naive saws and the band limiting are done separately.
//  - The naive pass is 4 samples per vector with every saw's phase vector in a register. Each side saw costs an add
//    and a vsra (shift right and accumulate, by just enough that the side saws can't overflow), the centre saw just
//    an add, then two vqdmulh for the mix gains. It's templated on the number of saws, picked once per buffer, so
//    each count gets a fully unrolled loop. Only one is hot per voice.
//  - The polyBLEP only touches the two samples either side of each falling edge. Each saw jumps from edge to edge,
//    working out the distance to the next one from its phase with a float reciprocal, corrected exactly with the
//    integer remainder. Cost goes with the number of edges, so it's next to nothing for bass and for normal melody
//    ranges is about half the naive pass.
//  - Per-saw increments and their reciprocals are worked out per buffer as two float vectors, using the reciprocal
//    estimate plus two Newton steps rather than seven VFP divides.
//  - Spread is taken once per buffer rather than interpolated per sample. Params only update per buffer anyway, and
//    with per-sample increments the BLEP would need a reciprocal per sample.
//
// Alternatives considered:
//  - A branchless vector polyBLEP on every sample of every saw. Simple, but about 10 extra vector ops per saw per 4
//    samples, which costs more than the edge-to-edge version until notes get to several kHz.
//  - Saws in lanes rather than samples in lanes (2 vectors for 7 saws). Fewer phase registers, but it needs a
//    horizontal add per sample and getting that back out of NEON stalls the A9.
//  - Fusing the amplitude ramp into the naive pass. The BLEP corrections then need the amplitude at each edge too,
//    and the saving is one load and store per 4 samples, so the caller applies amplitude afterwards like wavetables.
//  - One loop for all saw counts with a runtime count. Unused saws would still cost their ops, or the loop would
//    need a branch per saw.
//  - Per-saw gains. Szabo's model has one side gain, so the side saws share an accumulator and a multiply.
void Supersaw::render(int32_t* __restrict__ buffer, int32_t numSamples, uint32_t phaseIncrement, int32_t spread,
                      int32_t numSaws, uint32_t& centrePhase, SidePhases& sidePhases, bool bandLimit) {
	numSaws = std::clamp<int32_t>(numSaws, 1, kNumSaws);

	// Spread runs from 0 to kMaxSpread over the whole of the bipolar param range. Detune goes up with its square, so
	// there's finer control over the small detunes
	float x = static_cast<float>(std::clamp<int32_t>(spread, -(1 << 30), (1 << 30) - 1) + (1 << 30))
	          * (kMaxSpread * 0x1p-31f);
	float detune = x * x;
	float centreGain = centreMix(x);
	float sideGain = sideMix(x);
	float norm = 1.f / std::sqrt(centreGain * centreGain + static_cast<float>(numSaws - 1) * sideGain * sideGain);
	centreGain *= norm;
	sideGain *= norm;

	alignas(16) std::array<uint32_t, 8> increments;
	alignas(16) std::array<float, 8> reciprocals;
	for (size_t v = 0; v < increments.size(); v += 4) {
		Argon<float> ratio = Argon<float>{1.f}.MultiplyAdd(Argon<float>::Load(&kDetuneOffsets[v]), detune);
		Argon<float> sawIncrements = ratio * static_cast<float>(phaseIncrement);
		sawIncrements.ConvertTo<uint32_t>().StoreTo(&increments[v]);
		sawIncrements.ReciprocalEstimateRefine(2).StoreTo(&reciprocals[v]);
	}
	// Float only has 24 bits, keep the centre saw exactly in tune with the rest of the synth
	increments[0] = phaseIncrement;

	std::array<uint32_t, kNumSaws> phases;
	phases[0] = centrePhase;
	std::ranges::copy(sidePhases, phases.begin() + 1);

	switch (numSaws) {
	case 1:
		renderNaive<1>(buffer, numSamples, phases.data(), increments.data(), centreGain, sideGain);
		break;
	case 2:
		renderNaive<2>(buffer, numSamples, phases.data(), increments.data(), centreGain, sideGain);
		break;
	case 3:
		renderNaive<3>(buffer, numSamples, phases.data(), increments.data(), centreGain, sideGain);
		break;
	case 4:
		renderNaive<4>(buffer, numSamples, phases.data(), increments.data(), centreGain, sideGain);
		break;
	case 5:
		renderNaive<5>(buffer, numSamples, phases.data(), increments.data(), centreGain, sideGain);
		break;
	case 6:
		renderNaive<6>(buffer, numSamples, phases.data(), increments.data(), centreGain, sideGain);
		break;
	default:
		renderNaive<7>(buffer, numSamples, phases.data(), increments.data(), centreGain, sideGain);
		break;
	}

	if (bandLimit) {
		// A residual of 1 is half the jump of one saw, in the same units as the naive pass's output. Not unrolled,
		// one copy of the edge loop is plenty and keeps it in cache
		const float centreScale = centreGain * 0x1p29f;
		const float sideScale = sideGain * 0x1p29f;
#pragma GCC unroll 1
		for (int32_t i = 0; i < numSaws; i++) {
			addBlepResiduals(buffer, numSamples, phases[i], increments[i], reciprocals[i],
			                 i == 0 ? centreScale : sideScale);
		}
	}

	// Saws that aren't playing hold their phase
	centrePhase = phases[0] + increments[0] * numSamples;
	for (int32_t i = 1; i < numSaws; i++) {
		sidePhases[i - 1] = phases[i] + increments[i] * numSamples;
	}
}

} // namespace deluge::dsp
