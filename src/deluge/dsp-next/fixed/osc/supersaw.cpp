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
#include "dsp-next/fixed/osc/supersaw.h"
#include <algorithm>
#include <argon.hpp>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace deluge::dsp::fixed {
// Named rather than anonymous: dsp-next is a unity build
namespace supersaw_internal {

constexpr double constexpr_sqrt(double v) {
	double r = std::max(v, 1.0);
	for (int32_t i = 0; i < 32; i++) {
		r = 0.5 * (r + v / r);
	}
	return r;
}

/// The side saws are summed shifted right by this, the least that can't overflow
constexpr int32_t side_shift(size_t num_saws) {
	return std::bit_width(num_saws - 2);
}

/// Gains normalised so the summed (uncorrelated) saws have the RMS of a single saw, so spread and saw count change
/// the sound without changing the level
constexpr std::pair<double, double> normalised_gains(float spread, size_t num_saws) {
	const auto centre = static_cast<double>(centre_mix(spread));
	const auto side = static_cast<double>(side_mix(spread));
	double norm = 1.0 / constexpr_sqrt(centre * centre + (num_saws - 1) * side * side);
	return {centre * norm, side * norm};
}

// The side gain goes to Q(29 + shift) and has to fit, and the worst case sum of all saws has to fit in an int32
static_assert([] {
	for (int32_t i = 0; i <= 1024; i++) {
		for (size_t num_saws = 2; num_saws <= Supersaw::kNumSaws; num_saws++) {
			auto [centre, side] = normalised_gains(kMaxSpread * i / 1024.f, num_saws);
			if (side * (1u << side_shift(num_saws)) >= 4 || centre + (num_saws - 1) * side >= 4) {
				return false;
			}
		}
	}
	return true;
}());

#ifndef __arm__
/// The A9's VRECPE.F32: an 8-bit reciprocal estimate (ARM ARM FPRecipEstimate and RecipEstimate, in the standard FPSCR
/// mode NEON uses: denormals flush to zero). SIMDe returns the exact reciprocal instead, which the refinement steps do
/// not fully converge from, so the host computes the device's estimate.
[[nodiscard]] float a9_reciprocal_estimate(float value) {
	constexpr uint32_t kSign = 0x80000000u;
	constexpr uint32_t kInfinity = 0x7F800000u;
	constexpr uint32_t kDefaultNaN = 0x7FC00000u;
	constexpr int32_t kEstimateExponentBase = 253; // the estimate's exponent is this minus the operand's
	const auto bits = std::bit_cast<uint32_t>(value);
	const uint32_t sign = bits & kSign;
	const auto exponent = static_cast<int32_t>((bits >> 23) & 0xFF);
	const uint32_t fraction = bits & 0x7FFFFFu;
	if (exponent == 0xFF) {
		return std::bit_cast<float>(fraction != 0 ? kDefaultNaN : sign);
	}
	if (exponent == 0) {
		return std::bit_cast<float>(sign | kInfinity); // zero, or a denormal flushed to zero
	}
	if (exponent >= kEstimateExponentBase) {
		return std::bit_cast<float>(sign); // at least 2^126: the reciprocal would be denormal, flushed to zero
	}
	const uint32_t scaled = 256 + (fraction >> 15);                      // the significand to 8 bits, in [256, 512)
	const uint32_t estimate = (((1u << 19) / (scaled * 2 + 1)) + 1) / 2; // 512 / significand, rounded, in [256, 512)
	const auto estimate_exponent = static_cast<uint32_t>(kEstimateExponentBase - exponent);
	return std::bit_cast<float>(sign | (estimate_exponent << 23) | ((estimate & 0xFF) << 15));
}
#endif

/// The reciprocal of each lane: the estimate refined by two Newton steps, as the device computes it.
[[nodiscard]] Argon<float> reciprocal(Argon<float> value) {
#ifdef __arm__
	return value.ReciprocalEstimateRefine(2);
#else
	// Each step is VRECPS (2 - value * estimate, rounded after the multiply and again after the subtract, never fused)
	// and a multiply.
	alignas(16) std::array<float, 4> lanes{};
	value.StoreTo(lanes.data());
	for (float& lane : lanes) {
		float estimate = a9_reciprocal_estimate(lane);
		for (int32_t i = 0; i < 2; i++) {
			const float product = lane * estimate;
			const float step = 2.f - product;
			estimate *= step;
		}
		lane = estimate;
	}
	return Argon<float>::Load(lanes.data());
#endif
}

/// The naive (aliasing) saws, mixed, 4 samples per vector. Templated on the number of saws so each count gets its own
/// fully unrolled loop with every phase in a register.
template <size_t kSaws>
[[gnu::hot]] void render_naive(int32_t* __restrict__ buffer, int32_t num_samples, const uint32_t* phases,
                               const uint32_t* increments, float centre_gain, float side_gain) {
	constexpr int32_t kShift = kSaws > 1 ? side_shift(kSaws) : 0;

	// Both gains in one register, multiplied in by lane, otherwise GCC moves them over from core registers every loop.
	// Centre saw is used at full scale, the side saws are summed at 1/2^kShift scale
	Argon<int32_t> gains{static_cast<int32_t>(centre_gain * 0x1p29f),
	                     static_cast<int32_t>(side_gain * 0x1p29f * (1 << kShift)), 0, 0};

	std::array<Argon<uint32_t>, kSaws> phase_vectors;
	std::array<Argon<uint32_t>, kSaws> step_vectors;
#pragma GCC unroll 7
	for (size_t i = 0; i < kSaws; i++) {
		phase_vectors[i] = Argon<uint32_t>{phases[i]}.MultiplyAdd(uint32x4_t{1, 2, 3, 4}, increments[i]);
		step_vectors[i] = Argon<uint32_t>{increments[i] * 4};
	}

	int32_t* __restrict__ out = buffer;
	int32_t const* const end = buffer + num_samples;
	do {
		Argon<int32_t> mix = phase_vectors[0].template As<int32_t>().MultiplyFixedQMax(gains[0]);
		if constexpr (kSaws > 1) {
			Argon<int32_t> sides = phase_vectors[1].template As<int32_t>();
			if constexpr (kShift > 0) {
				sides = sides.ShiftRight<kShift>();
			}
			if constexpr (kSaws > 2) { // A shift of 0 isn't a valid vsra, even in a loop that never runs
#pragma GCC unroll 7
				for (size_t i = 2; i < kSaws; i++) {
					sides = sides.ShiftRightAccumulate<kShift>(phase_vectors[i].template As<int32_t>());
				}
			}
			mix = mix + sides.MultiplyFixedQMax(gains[1]);
		}
		mix.StoreTo(out);

#pragma GCC unroll 7
		for (size_t i = 0; i < kSaws; i++) {
			phase_vectors[i] = phase_vectors[i] + step_vectors[i];
		}
		out += 4;
	} while (out < end);
}

/// Add the polyBLEP residual around every falling edge of one saw. The saw is (int32_t)phase, which falls from max to
/// min where phase crosses 2^31. Edges just past the end of the buffer still correct its last sample. That edge comes
/// round again at the start of the next buffer, which then does the sample after it.
[[gnu::always_inline]] inline void add_blep_residuals(int32_t* __restrict__ buffer, int32_t num_samples, uint32_t phase,
                                                      uint32_t increment, float reciprocal, float scale) {
	// Phase measured from the falling edge
	uint32_t x = phase ^ 0x80000000u;
	int32_t sample = -1;
	while (true) {
		// The next edge is floor(y / increment) + 1 samples on. The float estimate is within 1 of that, the
		// remainder fixes it up exactly
		uint32_t y = ~x;
		float estimate = static_cast<float>(y) * reciprocal;
		if (estimate >= static_cast<float>(num_samples - sample + 1)) {
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
		if (sample > num_samples) {
			return;
		}

		// How far past the edge this sample is, in samples
		x = increment - 1 - static_cast<uint32_t>(remainder);
		float t = std::min(static_cast<float>(x) * reciprocal, 1.f);

		if (sample > 0) {
			buffer[sample - 1] -= static_cast<int32_t>(t * t * scale);
		}
		if (sample < num_samples) {
			float u = 1.f - t;
			buffer[sample] += static_cast<int32_t>(u * u * scale);
		}
	}
}

} // namespace supersaw_internal

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
void Supersaw::render(Buffer out, uint32_t phase_increment, int32_t spread, int32_t num_saws, uint32_t& centre_phase,
                      SidePhases& side_phases, bool band_limit) {
	using namespace supersaw_internal;
	int32_t* __restrict__ buffer = out.data();
	const auto num_samples = static_cast<int32_t>(out.size());
	num_saws = std::clamp<int32_t>(num_saws, 1, kNumSaws);

	// Spread runs from 0 to kMaxSpread over the whole of the bipolar param range. Detune goes up with its square, so
	// there's finer control over the small detunes
	float x = static_cast<float>(std::clamp<int32_t>(spread, -(1 << 30), (1 << 30) - 1) + (1 << 30))
	          * (kMaxSpread * 0x1p-31f);
	float detune = x * x;
	float centre_gain = centre_mix(x);
	float side_gain = side_mix(x);
	float norm = 1.f / std::sqrt(centre_gain * centre_gain + static_cast<float>(num_saws - 1) * side_gain * side_gain);
	centre_gain *= norm;
	side_gain *= norm;

	alignas(16) std::array<uint32_t, 8> increments;
	alignas(16) std::array<float, 8> reciprocals;
	for (size_t v = 0; v < increments.size(); v += 4) {
		Argon<float> ratio = Argon<float>{1.f}.MultiplyAdd(Argon<float>::Load(&kDetuneOffsets[v]), detune);
		Argon<float> saw_increments = ratio * static_cast<float>(phase_increment);
		saw_increments.ConvertTo<uint32_t>().StoreTo(&increments[v]);
		reciprocal(saw_increments).StoreTo(&reciprocals[v]);
	}
	// Float only has 24 bits, keep the centre saw exactly in tune with the rest of the synth
	increments[0] = phase_increment;

	std::array<uint32_t, kNumSaws> phases;
	phases[0] = centre_phase;
	std::ranges::copy(side_phases, phases.begin() + 1);

	switch (num_saws) {
	case 1:
		render_naive<1>(buffer, num_samples, phases.data(), increments.data(), centre_gain, side_gain);
		break;
	case 2:
		render_naive<2>(buffer, num_samples, phases.data(), increments.data(), centre_gain, side_gain);
		break;
	case 3:
		render_naive<3>(buffer, num_samples, phases.data(), increments.data(), centre_gain, side_gain);
		break;
	case 4:
		render_naive<4>(buffer, num_samples, phases.data(), increments.data(), centre_gain, side_gain);
		break;
	case 5:
		render_naive<5>(buffer, num_samples, phases.data(), increments.data(), centre_gain, side_gain);
		break;
	case 6:
		render_naive<6>(buffer, num_samples, phases.data(), increments.data(), centre_gain, side_gain);
		break;
	default:
		render_naive<7>(buffer, num_samples, phases.data(), increments.data(), centre_gain, side_gain);
		break;
	}

	if (band_limit) {
		// A residual of 1 is half the jump of one saw, in the same units as the naive pass's output. Not unrolled,
		// one copy of the edge loop is plenty and keeps it in cache
		const float centre_scale = centre_gain * 0x1p29f;
		const float side_scale = side_gain * 0x1p29f;
#pragma GCC unroll 1
		for (int32_t i = 0; i < num_saws; i++) {
			add_blep_residuals(buffer, num_samples, phases[i], increments[i], reciprocals[i],
			                   i == 0 ? centre_scale : side_scale);
		}
	}

	// Saws that aren't playing hold their phase
	centre_phase = phases[0] + increments[0] * num_samples;
	for (int32_t i = 1; i < num_saws; i++) {
		side_phases[i - 1] = phases[i] + increments[i] * num_samples;
	}
}

} // namespace deluge::dsp::fixed
