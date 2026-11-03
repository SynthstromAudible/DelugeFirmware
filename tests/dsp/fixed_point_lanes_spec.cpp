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
#include "dsp-next/core/fixed_point_lanes.h"
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <type_traits>

using namespace deluge::dsp;

namespace {
template <int F, std::size_t N>
using P = LanePack<Q<F>, N>;

// Derived formats carry through, a scalar broadcasts, and a pack has no arithmetic operators.
static_assert(std::is_same_v<decltype(mul(P<31, 2>{}, P<31, 2>{})), P<30, 2>>);
static_assert(std::is_same_v<decltype(mul(P<31, 4>{}, Q<31>{})), P<30, 4>>);
static_assert(std::is_same_v<decltype(mul_wide(P<31, 2>{}, P<31, 2>{})), LanePack<Q64<62>, 2>>);
static_assert(std::is_same_v<decltype(as_wrap<Q<31>>(P<30, 2>{})), P<31, 2>>);
static_assert(std::is_same_v<decltype(P<31, 2>{} < P<31, 2>{}), LanePack<bool, 2>>);
static_assert(std::is_same_v<decltype(P<31, 2>{} == P<31, 2>{}), LanePack<bool, 2>>);
static_assert(std::is_same_v<decltype(P<31, 2>{} != Q<31>{}), LanePack<bool, 2>>);
static_assert(std::is_same_v<decltype(Q<31>{} >= P<31, 2>{}), LanePack<bool, 2>>);
static_assert(std::is_same_v<decltype(mul(P<31, 2>{}, Q16<15>{})), P<30, 2>>);
static_assert(std::is_same_v<decltype(mul(LanePack<UQ<32>, 2>{}, Q<31>{})), P<31, 2>>);
template <class A, class B>
concept addable = requires(A a, B b) { a + b; };
template <class A, class B>
concept lane_multipliable = requires(A a, B b) { mul(a, b); };
static_assert(!addable<P<31, 2>, P<31, 2>>);
static_assert(!addable<P<31, 2>, Q<31>>);
static_assert(lane_multipliable<P<31, 2>, Q<31>>);
static_assert(!lane_multipliable<P<31, 2>, P<31, 4>>);     // widths must agree
static_assert(!lane_multipliable<P<31, 2>, std::int32_t>); // a raw scalar is not a format
static_assert(!lane_multipliable<P<31, 2>, UQ<32>>);       // no scalar mul(Q, UQ) exists, so no lane one

// from_raw and to_raw convert a raw scalar or pack and a typed one, of the same width.
static_assert(std::is_same_v<decltype(from_raw<Q<31>>(std::int32_t{})), Q<31>>);
static_assert(std::is_same_v<decltype(from_raw<Q<30>>(LanePack<std::int32_t, 4>{})), P<30, 4>>);
static_assert(std::is_same_v<decltype(from_raw<UQ<32>>(LanePack<std::uint32_t, 2>{})), LanePack<UQ<32>, 2>>);
static_assert(std::is_same_v<decltype(to_raw(Q<31>{})), std::int32_t>);
static_assert(std::is_same_v<decltype(to_raw(P<29, 2>{})), LanePack<std::int32_t, 2>>);
template <class T, class V>
concept raw_convertible = requires(V v) { from_raw<T>(v); };
static_assert(!raw_convertible<Q<31>, std::uint32_t>); // a raw value converts only from its format's storage
static_assert(!raw_convertible<Q<31>, LanePack<std::uint32_t, 2>>);

std::uint32_t xorshift(std::uint32_t& s) {
	s ^= s << 13;
	s ^= s >> 17;
	s ^= s << 5;
	return s;
}

template <std::size_t N>
P<31, N> random_pack(std::uint32_t& s) {
	P<31, N> p;
	for (std::size_t i = 0; i < N; ++i) {
		p.lane[i] = Q<31>::from_raw(static_cast<std::int32_t>(xorshift(s)));
	}
	return p;
}

/// Lanes whose bits change on a round trip from raw to typed and back, over 4096 random packs of width N.
template <std::size_t N>
std::size_t round_trip_mismatches() {
	std::size_t bad = 0;
	std::uint32_t s = 0x2468'ace1u;
	for (int k = 0; k < 4096; ++k) {
		const auto typed = random_pack<N>(s);
		const LanePack<std::int32_t, N> raw = to_raw(typed);
		const P<31, N> back = from_raw<Q<31>>(raw);
		for (std::size_t i = 0; i < N; ++i) {
			bad += raw.lane[i] != typed.lane[i].raw() || back.lane[i] != typed.lane[i] ? 1 : 0;
		}
	}
	return bad;
}

/// Mismatches between a lane operation and its scalar operation over 20 000 random packs of width N, lane 0 of the
/// first 49 also driven through every pair of edge values.
template <std::size_t N, class LaneOp, class ScalarOp>
std::size_t mismatches(LaneOp lane_op, ScalarOp scalar_op) {
	std::size_t bad = 0;
	std::uint32_t s = 0x1234'5678u;
	const std::int32_t edges[] = {0, 1, -1, INT32_MAX, INT32_MIN, 1 << 30, -(1 << 30)};
	for (int k = 0; k < 20000; ++k) {
		auto a = random_pack<N>(s);
		auto b = random_pack<N>(s);
		auto c = random_pack<N>(s);
		if (k < 49) {
			a.lane[0] = Q<31>::from_raw(edges[k % 7]);
			b.lane[0] = Q<31>::from_raw(edges[k / 7]);
		}
		const auto got = lane_op(a, b, c);
		for (std::size_t i = 0; i < N; ++i) {
			bad += got.lane[i] != scalar_op(a.lane[i], b.lane[i], c.lane[i]) ? 1 : 0;
		}
	}
	return bad;
}

/// A Q<30> accumulator from the same random bits, as a scalar and as a pack.
Q<30> q30(Q<31> c) {
	return reinterpret_bits<Q<30>>(c);
}
template <std::size_t N>
P<30, N> q30_lanes(P<31, N> c) {
	P<30, N> r;
	for (std::size_t i = 0; i < N; ++i) {
		r.lane[i] = q30(c.lane[i]);
	}
	return r;
}

/// A lane operation and its scalar twin: one generic lambda serves both, called on packs and on lanes.
#define BOTH(EXPR)                                                                                                     \
	[](auto a, auto b, auto c) {                                                                                       \
		(void)b;                                                                                                       \
		(void)c;                                                                                                       \
		return EXPR;                                                                                                   \
	}

template <std::size_t N>
std::size_t all_mismatches() {
	std::size_t bad = 0;
	bad += mismatches<N>(BOTH(add_wrap(a, b)), BOTH(add_wrap(a, b)));
	bad += mismatches<N>(BOTH(sub_wrap(a, b)), BOTH(sub_wrap(a, b)));
	bad += mismatches<N>(BOTH(neg_wrap(a)), BOTH(neg_wrap(a)));
	bad += mismatches<N>(BOTH(add_sat(a, b)), BOTH(add_sat(a, b)));
	bad += mismatches<N>(BOTH(sub_sat(a, b)), BOTH(sub_sat(a, b)));
	bad += mismatches<N>(BOTH(neg_sat(a)), BOTH(neg_sat(a)));
	bad += mismatches<N>(BOTH(add_double_sat(a, b)), BOTH(add_double_sat(a, b)));
	bad += mismatches<N>(BOTH(sub_double_sat(a, b)), BOTH(sub_double_sat(a, b)));
	bad += mismatches<N>(BOTH(add_halves_sat(a, b)), BOTH(add_halves_sat(a, b)));
	bad += mismatches<N>(BOTH(clamp_high(a, b)), BOTH(clamp_high(a, b)));
	bad += mismatches<N>(BOTH(clamp_bits<24>(a)), BOTH(clamp_bits<24>(a)));
	bad += mismatches<N>(BOTH(clamp_unsigned_bits<16>(a)), BOTH(clamp_unsigned_bits<16>(a)));
	bad += mismatches<N>(BOTH(mul(a, b)), BOTH(mul(a, b)));
	bad += mismatches<N>(BOTH(mul(a, Q<31>::from_raw(0x5555'5555))), BOTH(mul(a, Q<31>::from_raw(0x5555'5555))));
	bad += mismatches<N>(BOTH(mul_r(a, b)), BOTH(mul_r(a, b)));
	bad += mismatches<N>(BOTH(mla(q30_lanes(c), a, b)), BOTH(mla(q30(c), a, b)));
	bad += mismatches<N>(BOTH(mla_r(q30_lanes(c), a, b)), BOTH(mla_r(q30(c), a, b)));
	bad += mismatches<N>(BOTH(mls(q30_lanes(c), a, b)), BOTH(mls(q30(c), a, b)));
	bad += mismatches<N>(BOTH(mls_r(q30_lanes(c), a, b)), BOTH(mls_r(q30(c), a, b)));
	bad += mismatches<N>(BOTH(mul_wide(a, b)), BOTH(mul_wide(a, b)));
	bad += mismatches<N>(BOTH(mla_wide(mul_wide(c, c), a, b)), BOTH(mla_wide(mul_wide(c, c), a, b)));
	bad += mismatches<N>(BOTH(mul_doubling_sat(a, b)), BOTH(mul_doubling_sat(a, b)));
	bad += mismatches<N>(BOTH(mul_doubling_r_sat(a, b)), BOTH(mul_doubling_r_sat(a, b)));
	bad += mismatches<N>(BOTH(as_floor<Q<28>>(a)), BOTH(as_floor<Q<28>>(a)));
	bad += mismatches<N>(BOTH(as_round<Q<28>>(a)), BOTH(as_round<Q<28>>(a)));
	bad += mismatches<N>(BOTH(as_wrap<Q<31>>(mul(a, b))), BOTH(as_wrap<Q<31>>(mul(a, b))));
	bad += mismatches<N>(BOTH(as_sat<Q<31>>(mul(a, b))), BOTH(as_sat<Q<31>>(mul(a, b))));
	bad += mismatches<N>(BOTH(as_clamp_then_shift<Q<31>>(as_floor<Q<28>>(a))),
	                     BOTH(as_clamp_then_shift<Q<31>>(as_floor<Q<28>>(a))));
	bad += mismatches<N>(BOTH(mul_as_wrap<Q<31>>(a, b)), BOTH(mul_as_wrap<Q<31>>(a, b)));
	bad += mismatches<N>(BOTH(mul_as_sat<Q<31>>(a, b)), BOTH(mul_as_sat<Q<31>>(a, b)));
	bad += mismatches<N>(BOTH(mul_r_as_wrap<Q<31>>(a, b)), BOTH(mul_r_as_wrap<Q<31>>(a, b)));
	bad += mismatches<N>(BOTH(scale_shl_wrap<3>(a)), BOTH(scale_shl_wrap<3>(a)));
	bad += mismatches<N>(BOTH(scale_shl_sat<3>(a)), BOTH(scale_shl_sat<3>(a)));
	bad += mismatches<N>(BOTH(scale_shl_clamp_then_shift<3>(a)), BOTH(scale_shl_clamp_then_shift<3>(a)));
	bad += mismatches<N>(BOTH(scale_shr_floor<3>(a)), BOTH(scale_shr_floor<3>(a)));
	bad += mismatches<N>(BOTH(scale_shr_round<3>(a)), BOTH(scale_shr_round<3>(a)));
	bad += mismatches<N>(BOTH(halve_floor(a)), BOTH(halve_floor(a)));
	bad += mismatches<N>(BOTH(halve_toward_zero(a)), BOTH(halve_toward_zero(a)));
	return bad;
}

/// Mismatches between UQ<32> lane operations (phases) and their scalar operations.
template <std::size_t N>
std::size_t phase_mismatches() {
	std::size_t bad = 0;
	std::uint32_t s = 0x0bad'cafeu;
	for (int k = 0; k < 20000; ++k) {
		LanePack<UQ<32>, N> a;
		LanePack<UQ<32>, N> b;
		for (std::size_t i = 0; i < N; ++i) {
			a.lane[i] = UQ<32>::from_raw(xorshift(s));
			b.lane[i] = UQ<32>::from_raw(xorshift(s));
		}
		const auto sum = add_wrap(a, b);
		const auto half = scale_shr_floor<1>(a);
		const auto doubled = scale_shl_wrap<1>(a);
		const auto gain = Q<31>::from_raw(static_cast<std::int32_t>(xorshift(s)));
		const auto scaled = mul(a, gain);
		for (std::size_t i = 0; i < N; ++i) {
			bad += sum.lane[i] != add_wrap(a.lane[i], b.lane[i]) ? 1 : 0;
			bad += half.lane[i] != scale_shr_floor<1>(a.lane[i]) ? 1 : 0;
			bad += doubled.lane[i] != scale_shl_wrap<1>(a.lane[i]) ? 1 : 0;
			bad += scaled.lane[i] != mul(a.lane[i], gain) ? 1 : 0;
		}
	}
	return bad;
}

/// Mismatches between each lane comparison and the scalar comparison.
template <std::size_t N>
std::size_t comparison_mismatches() {
	std::size_t bad = 0;
	std::uint32_t s = 0x9e37'79b9u;
	for (int k = 0; k < 20000; ++k) {
		const auto a = random_pack<N>(s);
		auto b = random_pack<N>(s);
		if (k % 4 == 0) {
			b.lane[0] = a.lane[0];
		}
		const auto lt = a < b;
		const auto gt = a > b;
		const auto le = a <= b;
		const auto ge = a >= b;
		const auto eq = a == b;
		const auto ne = a != b;
		for (std::size_t i = 0; i < N; ++i) {
			const auto x = a.lane[i];
			const auto y = b.lane[i];
			bad += (lt.lane[i] != (x < y)) + (gt.lane[i] != (x > y)) + (le.lane[i] != (x <= y))
			       + (ge.lane[i] != (x >= y)) + (eq.lane[i] != (x == y)) + (ne.lane[i] != (x != y));
		}
	}
	return bad;
}
} // namespace

// clang-format off
describe fixed_point_lanes("fixed_point lane packs", ${
	it("convert raw and typed lanes both ways with the bits unchanged, at every width", _ {
		expect(round_trip_mismatches<1>()).to_equal(std::size_t{0});
		expect(round_trip_mismatches<2>()).to_equal(std::size_t{0});
		expect(round_trip_mismatches<4>()).to_equal(std::size_t{0});
		expect(from_raw<Q<31>>(INT32_MIN).raw()).to_equal(INT32_MIN);
		expect(to_raw(Q<31>::max())).to_equal(INT32_MAX);
	});
	it("apply each scalar operation lane by lane, at every width", _ {
		expect(all_mismatches<1>()).to_equal(std::size_t{0});
		expect(all_mismatches<2>()).to_equal(std::size_t{0});
		expect(all_mismatches<4>()).to_equal(std::size_t{0});
	});
	it("apply UQ<32> operations lane by lane, at every width", _ {
		expect(phase_mismatches<1>()).to_equal(std::size_t{0});
		expect(phase_mismatches<2>()).to_equal(std::size_t{0});
		expect(phase_mismatches<4>()).to_equal(std::size_t{0});
	});
	it("compare lane by lane, at every width", _ {
		expect(comparison_mismatches<1>()).to_equal(std::size_t{0});
		expect(comparison_mismatches<2>()).to_equal(std::size_t{0});
		expect(comparison_mismatches<4>()).to_equal(std::size_t{0});
	});
	it("broadcast a scalar operand to every lane, on either side", _ {
		P<31, 2> x;
		x.lane[0] = Q<31>::from_raw(1 << 30);
		x.lane[1] = Q<31>::from_raw(-(1 << 30));
		const auto half = Q<31>::from_raw(1 << 30);
		const auto y = mul(x, half);
		expect(y.lane[0].raw()).to_equal(1 << 28);
		expect(y.lane[1].raw()).to_equal(-(1 << 28));
		const auto z = mul(half, x);
		expect(z.lane[1].raw()).to_equal(-(1 << 28));
		const auto w = add_sat(Q<31>::max(), x);
		expect(w.lane[0].raw()).to_equal(INT32_MAX);
		expect(w.lane[1].raw()).to_equal(INT32_MAX - (1 << 30));
		const auto q16 = mul(x, Q16<15>::from_raw(1 << 14));
		expect(q16.lane[1].raw()).to_equal(-(1 << 28));
	});
	it("give a mask select reads", _ {
		P<31, 2> a;
		P<31, 2> b;
		a.lane[0] = Q<31>::from_raw(1);
		a.lane[1] = Q<31>::from_raw(5);
		b.lane[0] = Q<31>::from_raw(2);
		b.lane[1] = Q<31>::from_raw(2);
		const auto lt = a < b;
		expect(lt.lane[0]).to_be_true();
		expect(lt.lane[1]).to_be_false();
		const auto lower = select(lt, a, b);
		expect(lower.lane[0].raw()).to_equal(1);
		expect(lower.lane[1].raw()).to_equal(2);
		const auto ceiling = Q<31>::from_raw(3);
		const auto over = a > ceiling;
		expect(over.lane[0]).to_be_false();
		expect(over.lane[1]).to_be_true();
		const auto clamped = select(over, P<31, 2>::broadcast(ceiling), a);
		expect(clamped.lane[1].raw()).to_equal(3);
		const auto under = ceiling > a;
		expect(under.lane[0]).to_be_true();
	});
	it("evaluate at compile time", _ {
		constexpr auto c = add_sat(P<31, 2>::broadcast(Q<31>::max()), Q<31>::one());
		static_assert(c.lane[1].raw() == INT32_MAX);
		expect(c.lane[1].raw()).to_equal(INT32_MAX);
	});
});
// clang-format on

CPPSPEC_MAIN(fixed_point_lanes);
