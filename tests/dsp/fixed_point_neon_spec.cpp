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
#include "dsp-next/core/fixed_point_neon.h"
#include <array>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <type_traits>

using namespace deluge::dsp;

namespace {
// Derived formats carry through, and a vector has no arithmetic operators.
static_assert(std::is_same_v<decltype(mul_doubling_sat(QVec<31>{}, QVec<30>{})), QVec<30>>);
static_assert(std::is_same_v<decltype(mul_doubling_sat(QVec<31>{}, Q<29>{})), QVec<29>>);
static_assert(std::is_same_v<decltype(mul_doubling_sat(Q16Vec<15>{}, Q16<15>{})), Q16Vec<15>>);
static_assert(std::is_same_v<decltype(mul_doubling_r_sat(QVec<31>{}, QVec<31>{})), QVec<31>>);
static_assert(std::is_same_v<decltype(as_floor<QVec<30>>(QVec<31>{})), QVec<30>>);
static_assert(std::is_same_v<decltype(reinterpret_bits<QVec<31>>(UQVec<32>{})), QVec<31>>);
static_assert(std::is_same_v<decltype(sum_wrap(QVec<30>{})), Q<30>>);
static_assert(std::is_same_v<decltype(add_wrap(UQVec<32>{}, UQVec<32>{})), UQVec<32>>);
static_assert(std::is_same_v<QVec<31>::register_type, Argon<std::int32_t>>);
static_assert(std::is_same_v<Q16Half<15>::register_type, ArgonHalf<std::int16_t>>);
static_assert(std::is_same_v<Q16x2Vec<15>::register_type, Argon<std::uint32_t>>);
template <class A, class B>
concept addable = requires(A a, B b) { a + b; };
template <class A, class B>
concept doubling_multipliable = requires(A a, B b) { mul_doubling_sat(a, b); };
static_assert(!addable<QVec<31>, QVec<31>>);
static_assert(!doubling_multipliable<QVec<31>, Q16Vec<15>>);          // lane widths must agree
static_assert(!doubling_multipliable<QVec<31>, Argon<std::int32_t>>); // a raw register is not a format

std::uint32_t xorshift(std::uint32_t& s) {
	s ^= s << 13;
	s ^= s >> 17;
	s ^= s << 5;
	return s;
}

constexpr std::array<std::int32_t, 5> kEdges32{0, 1, -1, INT32_MIN, INT32_MAX};
constexpr std::array<std::int16_t, 5> kEdges16{0, 1, -1, INT16_MIN, INT16_MAX};
constexpr int kRounds = 1 << 12;

/// A vector's lanes: random, except that the first rounds put each pair of edge values in lane 0 of a and b.
template <class Scalar, std::size_t N, std::size_t E>
std::array<Scalar, N> lanes(std::uint32_t& s, int round, const std::array<Scalar, E>& edges, bool first) {
	std::array<Scalar, N> v;
	for (auto& x : v) {
		x = static_cast<Scalar>(xorshift(s));
	}
	if (round < static_cast<int>(E * E)) {
		v[0] = edges[first ? round % E : round / E];
	}
	return v;
}

std::int32_t wrap32(std::int64_t v) {
	return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::uint64_t>(v)));
}
std::int16_t sat16(std::int64_t v) {
	return static_cast<std::int16_t>(v > INT16_MAX ? INT16_MAX : (v < INT16_MIN ? INT16_MIN : v));
}

/// Mismatches between a 32-bit lane operation and its per-lane reference over kRounds random vectors.
template <class VecOp, class LaneOp>
std::size_t mismatches32(VecOp vec_op, LaneOp lane_op) {
	std::size_t bad = 0;
	std::uint32_t s = 0x1234'5678u;
	for (int k = 0; k < kRounds; ++k) {
		const auto a = lanes<std::int32_t, 4>(s, k, kEdges32, true);
		const auto b = lanes<std::int32_t, 4>(s, k, kEdges32, false);
		const auto got = vec_op(Argon<std::int32_t>::Load(a.data()), Argon<std::int32_t>::Load(b.data()), b[1]);
		for (std::size_t i = 0; i < 4; ++i) {
			bad += got[i] != lane_op(a[i], b[i], b[1]) ? 1 : 0;
		}
	}
	return bad;
}

template <int F>
QVec<F> vec(Argon<std::int32_t> r) {
	return QVec<F>::from_raw(r);
}
template <int F>
Q<F> q(std::int32_t r) {
	return Q<F>::from_raw(r);
}
template <class V>
std::array<std::int32_t, 4> out(V v) {
	return {v.lane(0).raw(), v.lane(1).raw(), v.lane(2).raw(), v.lane(3).raw()};
}

std::size_t arithmetic_mismatches() {
	std::size_t bad = 0;
	bad += mismatches32([](auto a, auto b, auto) { return out(add_wrap(vec<31>(a), vec<31>(b))); },
	                    [](auto a, auto b, auto) { return add_wrap(q<31>(a), q<31>(b)).raw(); });
	bad += mismatches32([](auto a, auto b, auto) { return out(sub_wrap(vec<31>(a), vec<31>(b))); },
	                    [](auto a, auto b, auto) { return sub_wrap(q<31>(a), q<31>(b)).raw(); });
	bad += mismatches32([](auto a, auto b, auto) { return out(mul_doubling_sat(vec<31>(a), vec<30>(b))); },
	                    [](auto a, auto b, auto) { return mul_doubling_sat(q<31>(a), q<30>(b)).raw(); });
	bad += mismatches32([](auto a, auto, auto c) { return out(mul_doubling_sat(vec<31>(a), q<29>(c))); },
	                    [](auto a, auto, auto c) { return mul_doubling_sat(q<31>(a), q<29>(c)).raw(); });
	bad += mismatches32([](auto a, auto b, auto) { return out(mul_doubling_r_sat(vec<31>(a), vec<31>(b))); },
	                    [](auto a, auto b, auto) { return mul_doubling_r_sat(q<31>(a), q<31>(b)).raw(); });
	bad += mismatches32([](auto a, auto, auto) { return out(scale_shl_wrap<3>(vec<31>(a))); },
	                    [](auto a, auto, auto) { return scale_shl_wrap<3>(q<31>(a)).raw(); });
	bad += mismatches32([](auto a, auto, auto) { return out(scale_shr_floor<3>(vec<31>(a))); },
	                    [](auto a, auto, auto) { return scale_shr_floor<3>(q<31>(a)).raw(); });
	bad += mismatches32([](auto a, auto b, auto) { return out(add_scale_shr_floor<4>(vec<31>(a), vec<31>(b))); },
	                    [](auto a, auto b, auto) { return wrap32(std::int64_t{a} + (b >> 4)); });
	bad += mismatches32([](auto a, auto, auto) { return out(as_floor<QVec<28>>(vec<31>(a))); },
	                    [](auto a, auto, auto) { return as_floor<Q<28>>(q<31>(a)).raw(); });
	bad += mismatches32(
	    [](auto a, auto, auto) {
		    const auto phase = UQVec<32>::from_raw(vreinterpretq_u32_s32(a));
		    return out(reinterpret_bits<QVec<31>>(phase));
	    },
	    [](auto a, auto, auto) {
		    return reinterpret_bits<Q<31>>(UQ<32>::from_raw(static_cast<std::uint32_t>(a))).raw();
	    });
	return bad;
}

/// Mismatches between the UQ<32> phase add and the scalar one.
std::size_t phase_mismatches() {
	std::size_t bad = 0;
	std::uint32_t s = 0x0bad'cafeu;
	for (int k = 0; k < kRounds; ++k) {
		std::array<std::uint32_t, 4> a;
		std::array<std::uint32_t, 4> b;
		for (std::size_t i = 0; i < 4; ++i) {
			a[i] = xorshift(s);
			b[i] = xorshift(s);
		}
		const auto pa = UQVec<32>::from_raw(Argon<std::uint32_t>::Load(a.data()));
		const auto pb = UQVec<32>::from_raw(Argon<std::uint32_t>::Load(b.data()));
		const auto sum = add_wrap(pa, pb);
		const auto difference = sub_wrap(pa, pb);
		for (std::size_t i = 0; i < 4; ++i) {
			bad += sum.lane(i) != add_wrap(UQ<32>::from_raw(a[i]), UQ<32>::from_raw(b[i])) ? 1 : 0;
			bad += difference.lane(i) != sub_wrap(UQ<32>::from_raw(a[i]), UQ<32>::from_raw(b[i])) ? 1 : 0;
		}
	}
	return bad;
}

/// Mismatches between the 16-bit lane operations and their per-lane references.
std::size_t q16_mismatches() {
	std::size_t bad = 0;
	std::uint32_t s = 0x9e37'79b9u;
	for (int k = 0; k < kRounds; ++k) {
		const auto a = lanes<std::int16_t, 8>(s, k, kEdges16, true);
		const auto b = lanes<std::int16_t, 8>(s, k, kEdges16, false);
		const auto va = Q16Vec<15>::from_raw(Argon<std::int16_t>::Load(a.data()));
		const auto vb = Q16Vec<15>::from_raw(Argon<std::int16_t>::Load(b.data()));
		const auto scalar = Q16<15>::from_raw(b[0]);
		const auto sum = add_wrap(va, vb);
		const auto difference = sub_wrap(va, vb);
		const auto product = mul_doubling_sat(va, scalar);
		for (std::size_t i = 0; i < 8; ++i) {
			bad += sum.lane(i) != add_wrap(Q16<15>::from_raw(a[i]), Q16<15>::from_raw(b[i])) ? 1 : 0;
			bad += difference.lane(i) != sub_wrap(Q16<15>::from_raw(a[i]), Q16<15>::from_raw(b[i])) ? 1 : 0;
			bad += product.lane(i).raw() != sat16((2 * std::int64_t{a[i]} * b[0]) >> 16) ? 1 : 0;
		}
	}
	return bad;
}
} // namespace

// clang-format off
describe fixed_point_neon("fixed_point NEON vectors", ${
	it("apply each 32-bit operation lane by lane, as the scalar operation", _ {
		expect(arithmetic_mismatches()).to_equal(std::size_t{0});
	});
	it("add and subtract phases lane by lane, wrapping", _ {
		expect(phase_mismatches()).to_equal(std::size_t{0});
	});
	it("sum four lanes, wrapping", _ {
		std::uint32_t s = 0x5eed'1234u;
		std::size_t bad = 0;
		for (int k = 0; k < kRounds; ++k) {
			const auto a = lanes<std::int32_t, 4>(s, k, kEdges32, true);
			std::int64_t total = 0;
			for (const auto x : a) {
				total += x;
			}
			bad += sum_wrap(vec<30>(Argon<std::int32_t>::Load(a.data()))).raw() != wrap32(total) ? 1 : 0;
		}
		expect(bad).to_equal(std::size_t{0});
		const std::array<std::int32_t, 4> maxima{INT32_MAX, INT32_MAX, 1, 0};
		expect(sum_wrap(vec<31>(Argon<std::int32_t>::Load(maxima.data()))).raw()).to_equal(-1);
	});
	it("apply the 16-bit operations lane by lane", _ {
		expect(q16_mismatches()).to_equal(std::size_t{0});
	});
	it("saturate only min × min in the doubling multiplies", _ {
		const auto min = QVec<31>::splat(Q<31>::min());
		expect(mul_doubling_sat(min, min).lane(0).raw()).to_equal(INT32_MAX);
		expect(mul_doubling_r_sat(min, min).lane(3).raw()).to_equal(INT32_MAX);
		expect(mul_doubling_sat(Q16Vec<15>::splat(Q16<15>::min()), Q16<15>::min()).lane(7).raw())
		    .to_equal(std::int16_t{INT16_MAX});
	});
	it("splat a lane into every lane", _ {
		const auto x = Q16x2Vec<15>::splat(Q16x2<15>::from_raw(0x8000'7fffu));
		expect(x.lane(2).raw()).to_equal(0x8000'7fffu);
	});
});
// clang-format on

CPPSPEC_MAIN(fixed_point_neon);
