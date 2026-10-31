/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// The matrix core's four-lane forms against its scalar functions, lane for lane: operand corners and random operands,
// and every param the four-lane curves compute, with corner and random presets, on the host and on ARM.

#include "alloc_seams.h"

#include "synth/matrix/matrix_fixed.h"

#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

using namespace deluge::synth;
using namespace deluge::synth::matrix;
namespace params = deluge::modulation::params;

void functionsInit(); // util/param_functions.cpp: fills the param scaling tables

namespace {

using Lanes = MatrixFixed::Lanes;
constexpr int32_t kMin = std::numeric_limits<int32_t>::min();
constexpr int32_t kMax = std::numeric_limits<int32_t>::max();

constexpr std::array<int32_t, 19> kCorners{
    kMin,
    kMax,
    kMin + 1,
    kMax - 1,
    1 << 30,
    -(1 << 30),
    (1 << 30) + 1,
    -(1 << 30) - 1,
    1 << 29,
    -(1 << 29),
    1 << 26,
    -(1 << 26),
    0,
    1,
    -1,
    -3,
    -5,
    -7,
    -1234567,
};

/// xorshift32: every bit of its output varies.
struct Random {
	uint32_t state = 0x9E3779B9u;
	int32_t next() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return static_cast<int32_t>(state);
	}
	/// A value in [-2^bits, 2^bits].
	int32_t within(int32_t bits) {
		const auto span = static_cast<int64_t>(1) << (bits + 1);
		return static_cast<int32_t>((static_cast<int64_t>(static_cast<uint32_t>(next())) % (span + 1)) - (span / 2));
	}
};

/// The operand pairs: every pair of corners, then random operands.
std::vector<std::array<int32_t, 2>> operand_pairs(const std::function<int32_t(Random&)>& draw) {
	std::vector<std::array<int32_t, 2>> pairs;
	for (const int32_t a : kCorners) {
		for (const int32_t b : kCorners) {
			pairs.push_back({a, b});
		}
	}
	Random rng;
	for (int32_t i = 0; i < 100000; ++i) {
		pairs.push_back({draw(rng), draw(rng)});
	}
	return pairs;
}

/// How many lanes of @p vector differ from @p scalar over the operand groups of four.
template <class Vector, class Scalar>
int32_t lanes_differing(const std::vector<std::array<int32_t, 2>>& pairs, Vector vector, Scalar scalar) {
	int32_t bad = 0;
	for (std::size_t i = 0; i + 4 <= pairs.size(); i += 4) {
		alignas(16) std::array<int32_t, 4> a{};
		alignas(16) std::array<int32_t, 4> b{};
		alignas(16) std::array<int32_t, 4> out{};
		for (std::size_t l = 0; l < 4; ++l) {
			a[l] = pairs[i + l][0];
			b[l] = pairs[i + l][1];
		}
		vector(Lanes::Load(a.data()), Lanes::Load(b.data())).StoreTo(out.data());
		for (std::size_t l = 0; l < 4; ++l) {
			bad += out[l] != scalar(a[l], b[l]) ? 1 : 0;
		}
	}
	return bad;
}

/// Each param of [@p from, @p to) with random presets: @p vector's four-lane final values against the scalar chain
/// and curve.
template <class Vector>
int32_t region_differing(int32_t from, int32_t to, CurveKind kind, Vector vector) {
	int32_t bad = 0;
	Random rng;
	for (int32_t round = 0; round < 4000; ++round) {
		for (int32_t p = from; p + 4 <= to; ++p) {
			alignas(16) std::array<int32_t, 4> preset{};
			alignas(16) std::array<int32_t, 4> out{};
			for (int32_t l = 0; l < 4; ++l) {
				preset[l] = round < static_cast<int32_t>(kCorners.size()) ? kCorners[round] : rng.next();
			}
			const Lanes terms = MatrixFixed::preset_terms(Lanes::Load(preset.data()), Lanes::Load(&paramRanges[p]));
			vector(Lanes::Load(&paramNeutralValues[p]), terms).StoreTo(out.data());
			for (int32_t l = 0; l < 4; ++l) {
				const int32_t term = MatrixFixed::preset_term(preset[l], p + l);
				const int32_t combination =
				    chain_of(p + l) == ChainKind::MULTIPLY
				        ? MatrixFixed::multiply_step(MatrixFixed::kOne, term) - MatrixFixed::kOne
				        : MatrixFixed::add_step(0, term);
				bad += out[l] != MatrixFixed::curve(kind, p + l, combination) ? 1 : 0;
			}
		}
	}
	return bad;
}

} // namespace

// clang-format off
describe matrix_exactness_spec("the matrix core's four-lane forms", ${
	before_all([] { functionsInit(); });

	it("multiply high by doubling as smmul does, for a first operand that is never INT32_MIN", _{
		auto pairs = operand_pairs([](Random& r) { return r.next(); });
		std::erase_if(pairs, [](const auto& p) { return p[0] == kMin; });
		expect(lanes_differing(pairs, MatrixFixed::smmul_by_doubling, multiply_32x32_rshift32)).to_equal(0);
	});

	// The sound's linear and hybrid regions hold fewer than four params, so the evaluator runs them scalar
	it("give uncabled volume, linear and hybrid params their scalar final values", _{
		using MF = MatrixFixed;
		auto volume = [](Lanes n, Lanes t) { return MF::curve_volume(n, MF::uncabled_multiply(t)); };
		auto linear = [](Lanes n, Lanes t) { return MF::curve_linear(n, MF::uncabled_multiply(t)); };
		auto hybrid = [](Lanes n, Lanes t) { return MF::curve_hybrid(n, t); };
		expect(region_differing(0, params::FIRST_LOCAL_NON_VOLUME, CurveKind::VOLUME, volume)).to_equal(0);
		expect(region_differing(params::FIRST_LOCAL_NON_VOLUME, params::FIRST_LOCAL__HYBRID, CurveKind::LINEAR, linear))
		    .to_equal(0);
		expect(region_differing(params::FIRST_LOCAL__HYBRID, params::FIRST_LOCAL_EXP, CurveKind::HYBRID, hybrid))
		    .to_equal(0);
		expect(region_differing(params::FIRST_GLOBAL, params::FIRST_GLOBAL_NON_VOLUME, CurveKind::VOLUME, volume))
		    .to_equal(0);
	});
});
// clang-format on

CPPSPEC_MAIN(matrix_exactness_spec);
