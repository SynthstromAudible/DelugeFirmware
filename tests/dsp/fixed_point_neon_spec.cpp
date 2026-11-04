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
#include <bit>
#include <cppspec.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

using namespace deluge::dsp;

namespace {
// Derived formats carry through, and a vector has no arithmetic operators.
static_assert(std::is_same_v<decltype(mul_doubling_sat(QVec<31>{}, QVec<30>{})), QVec<30>>);
static_assert(std::is_same_v<decltype(mul_doubling_sat(QVec<31>{}, Q<29>{})), QVec<29>>);
static_assert(std::is_same_v<decltype(mul_doubling_sat(Q16Vec<15>{}, Q16<15>{})), Q16Vec<15>>);
static_assert(std::is_same_v<decltype(mul_doubling_r_sat(QVec<31>{}, QVec<31>{})), QVec<31>>);
static_assert(std::is_same_v<decltype(mul_doubling_r_sat(QVec<31>{}, Q<30>{})), QVec<30>>);
static_assert(std::is_same_v<decltype(as_floor<QVec<30>>(QVec<31>{})), QVec<30>>);
static_assert(std::is_same_v<decltype(reinterpret_bits<QVec<31>>(UQVec<32>{})), QVec<31>>);
static_assert(std::is_same_v<decltype(sum_wrap(QVec<30>{})), Q<30>>);
static_assert(std::is_same_v<decltype(add_scale_shr_floor<4>(QVec<31>{}, QVec<31>{})), QVec<31>>);
static_assert(std::is_same_v<decltype(mul_doubling_sat_lane<1>(QVec<31>{}, QVec<29>{})), QVec<29>>);
static_assert(std::is_same_v<decltype(add_wrap(UQVec<32>{}, UQVec<32>{})), UQVec<32>>);
static_assert(std::is_same_v<decltype(from_q16(Q16Half<15>{})), QVec<31>>);
static_assert(std::is_same_v<decltype(mul_doubling_wide_sat(Q16Half<15>{}, Q16Half<15>{})), QVec<31>>);
static_assert(std::is_same_v<decltype(mla_doubling_wide_sat(QVec<31>{}, Q16Half<15>{}, Q16Half<15>{})), QVec<31>>);
static_assert(std::is_same_v<decltype(mla_wide(QVec<30>{}, Q16Half<15>{}, Q16Half<15>{})), QVec<30>>);
static_assert(std::is_same_v<decltype(Q16x2Vec<15>{}.lo()), Q16Half<15>>);
static_assert(std::is_same_v<decltype(Q16Vec<14>{}.hi()), Q16Half<14>>);
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
std::int32_t sat32(std::int64_t v) {
	return static_cast<std::int32_t>(v > INT32_MAX ? INT32_MAX : (v < INT32_MIN ? INT32_MIN : v));
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
	bad += mismatches32([](auto a, auto, auto c) { return out(mul_doubling_r_sat(vec<31>(a), q<30>(c))); },
	                    [](auto a, auto, auto c) { return mul_doubling_r_sat(q<31>(a), q<30>(c)).raw(); });
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

/// Mismatches between the by-lane multiply by lane I and the scalar multiply by b's lane I, over kRounds random vectors
/// with the edge pairs in lane 0 of a and lane I of b.
template <int I>
std::size_t lane_mismatches() {
	std::size_t bad = 0;
	std::uint32_t s = 0x1a2b'3c4du;
	for (int k = 0; k < kRounds; ++k) {
		const auto a = lanes<std::int32_t, 4>(s, k, kEdges32, true);
		auto b = lanes<std::int32_t, 4>(s, k, kEdges32, false);
		std::swap(b[0], b[I]);
		const auto got = mul_doubling_sat_lane<I>(vec<31>(Argon<std::int32_t>::Load(a.data())),
		                                          vec<29>(Argon<std::int32_t>::Load(b.data())));
		for (std::size_t i = 0; i < 4; ++i) {
			bad += got.lane(i) != mul_doubling_sat(q<31>(a[i]), q<29>(b[I])) ? 1 : 0;
		}
	}
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
/// Mismatches between the widening 16-bit operations and their per-lane references.
std::size_t wide_mismatches() {
	std::size_t bad = 0;
	std::uint32_t s = 0x2545'f491u;
	for (int k = 0; k < kRounds; ++k) {
		const auto a = lanes<std::int16_t, 4>(s, k, kEdges16, true);
		const auto b = lanes<std::int16_t, 4>(s, k, kEdges16, false);
		const auto acc = lanes<std::int32_t, 4>(s, k, kEdges32, k % 2 == 0);
		const auto va = Q16Half<15>::from_raw(ArgonHalf<std::int16_t>::Load(a.data()));
		const auto vb = Q16Half<15>::from_raw(ArgonHalf<std::int16_t>::Load(b.data()));
		const auto raw_acc = Argon<std::int32_t>::Load(acc.data());
		const auto widened = from_q16(va);
		const auto product = mul_doubling_wide_sat(va, vb);
		const auto doubled_sum = mla_doubling_wide_sat(QVec<31>::from_raw(raw_acc), va, vb);
		const auto sum = mla_wide(QVec<30>::from_raw(raw_acc), va, vb);
		for (std::size_t i = 0; i < 4; ++i) {
			const std::int64_t doubled = sat32(2 * std::int64_t{a[i]} * b[i]);
			bad += widened.lane(i).raw() != std::int32_t{a[i]} * 65536 ? 1 : 0;
			bad += product.lane(i).raw() != doubled ? 1 : 0;
			bad += doubled_sum.lane(i).raw() != sat32(acc[i] + doubled) ? 1 : 0;
			bad += sum.lane(i).raw() != wrap32(acc[i] + (std::int64_t{a[i]} * b[i])) ? 1 : 0;
		}
	}
	return bad;
}

/// Mismatches between the half selections and the lanes they select.
std::size_t half_mismatches() {
	std::size_t bad = 0;
	std::uint32_t s = 0x7f4a'7c15u;
	for (int k = 0; k < kRounds; ++k) {
		std::array<std::uint32_t, 4> pairs;
		for (auto& p : pairs) {
			p = xorshift(s);
		}
		const auto words = Q16x2Vec<15>::from_raw(Argon<std::uint32_t>::Load(pairs.data()));
		const auto los = words.lo();
		const auto his = words.hi();
		const auto samples = lanes<std::int16_t, 8>(s, k, kEdges16, true);
		const auto vector = Q16Vec<15>::from_raw(Argon<std::int16_t>::Load(samples.data()));
		const auto low = vector.lo();
		const auto high = vector.hi();
		for (std::size_t i = 0; i < 4; ++i) {
			const auto pair = Q16x2<15>::from_raw(pairs[i]);
			bad += los.lane(i) != pair.lo() ? 1 : 0;
			bad += his.lane(i) != pair.hi() ? 1 : 0;
			bad += low.lane(i).raw() != samples[i] ? 1 : 0;
			bad += high.lane(i).raw() != samples[i + 4] ? 1 : 0;
		}
	}
	return bad;
}

/// A float's bits, so NaN and −0 compare as themselves.
std::uint32_t bits(float x) {
	return std::bit_cast<std::uint32_t>(x);
}

/// Mismatches between the vector fixed↔float conversions at Q<F> and the scalar ones, over edge values and floats
/// out of range, NaN and below the format's LSB.
template <int F>
std::size_t float_mismatches() {
	constexpr std::array<std::int32_t, 8> kRaw{0, 1, -1, 1 << 22, -(1 << 23), INT32_MAX, INT32_MIN, 3 << 20};
	const std::array<float, 8> floats{1.5F,       200.F,       -4.5e9F, std::numeric_limits<float>::quiet_NaN(),
	                                  0x1.8p-25F, -0x1.8p-25F, -0.F,    4.5e9F};
	std::size_t bad = 0;
	for (std::size_t base = 0; base < 8; base += 4) {
		const Argon<float> as_float = to_float(vec<F>(Argon<std::int32_t>::Load(kRaw.data() + base)));
		const auto back = from_float<F>(as_float);
		const auto from = from_float<F>(Argon<float>::Load(floats.data() + base));
		for (std::size_t i = 0; i < 4; ++i) {
			const auto lane = static_cast<float>(as_float[i]);
			bad += bits(lane) != bits(to_float(q<F>(kRaw[base + i]))) ? 1 : 0;
			bad += back.lane(i).raw() != from_float<F>(lane).raw() ? 1 : 0;
			bad += from.lane(i).raw() != from_float<F>(floats[base + i]).raw() ? 1 : 0;
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
	it("multiply by each lane of the other vector", _ {
		expect(lane_mismatches<0>()).to_equal(std::size_t{0});
		expect(lane_mismatches<1>()).to_equal(std::size_t{0});
		expect(lane_mismatches<2>()).to_equal(std::size_t{0});
		expect(lane_mismatches<3>()).to_equal(std::size_t{0});
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
	it("widen 16-bit lanes and their doubling and plain products", _ {
		expect(wide_mismatches()).to_equal(std::size_t{0});
	});
	it("select each pair's halves and each register's halves", _ {
		expect(half_mismatches()).to_equal(std::size_t{0});
	});
	it("saturate only min × min in the doubling multiplies", _ {
		const auto min = QVec<31>::splat(Q<31>::min());
		expect(mul_doubling_sat(min, min).lane(0).raw()).to_equal(INT32_MAX);
		expect(mul_doubling_r_sat(min, min).lane(3).raw()).to_equal(INT32_MAX);
		expect(mul_doubling_r_sat(min, Q<31>::min()).lane(1).raw()).to_equal(INT32_MAX);
		expect(mul_doubling_sat(min, Q<31>::min()).lane(2).raw()).to_equal(INT32_MAX);
		expect(mul_doubling_sat_lane<0>(min, min).lane(1).raw()).to_equal(INT32_MAX);
		expect(mul_doubling_sat_lane<3>(min, min).lane(0).raw()).to_equal(INT32_MAX);
		expect(mul_doubling_sat(Q16Vec<15>::splat(Q16<15>::min()), Q16<15>::min()).lane(7).raw())
		    .to_equal(std::int16_t{INT16_MAX});
		const auto min16 = Q16Half<15>::splat(Q16<15>::min());
		expect(mul_doubling_wide_sat(min16, min16).lane(0).raw()).to_equal(INT32_MAX);
		expect(mla_doubling_wide_sat(QVec<31>::splat(Q<31>::min()), min16, min16).lane(1).raw()).to_equal(-1);
	});
	it("convert to and from float lane by lane, as the scalar conversions", _ {
		expect(float_mismatches<22>()).to_equal(std::size_t{0});
		expect(float_mismatches<24>()).to_equal(std::size_t{0});
	});
	it("convert a float to fixed point truncating toward zero, saturating, NaN to 0", _ {
		const std::array<float, 4> edges{0x1.8p-24F, -0x1.8p-24F, 200.F, std::numeric_limits<float>::quiet_NaN()};
		const auto x = from_float<24>(Argon<float>::Load(edges.data()));
		expect(out(x)).to_equal(std::array<std::int32_t, 4>{1, -1, INT32_MAX, 0});
		expect(from_float<24>(Argon<float>{-200.F}).lane(0).raw()).to_equal(INT32_MIN);
	});
	it("load and store four lanes from an array of the lane type", _ {
		const std::array<Q<31>, 4> in{Q<31>::min(), Q<31>::from_raw(-1), Q<31>::from_raw(7), Q<31>::max()};
		std::array<Q<31>, 4> back{};
		QVec<31>::load(in.data()).store(back.data());
		expect(back == in).to_be_true();
		expect(QVec<31>::load(in.data()).lane(2).raw()).to_equal(7);
	});
	it("load the first lanes only, zeroing the rest", _ {
		// The fourth element is past the lanes asked for: it must not reach lane 3.
		const std::array<Q<31>, 4> in{Q<31>::from_raw(1), Q<31>::from_raw(-2), Q<31>::max(), Q<31>::from_raw(99)};
		const auto v = QVec<31>::load(in.data(), 3);
		expect(out(v)).to_equal(std::array<std::int32_t, 4>{1, -2, INT32_MAX, 0});
		const auto one = UQVec<32>::load(std::array<UQ<32>, 1>{UQ<32>::from_raw(5u)}.data(), 1);
		expect(one.lane(0).raw()).to_equal(5u);
		expect(one.lane(1).raw()).to_equal(0u);
	});
	it("store the first lanes only, leaving the rest unwritten", _ {
		std::array<Q<31>, 4> back;
		back.fill(Q<31>::from_raw(99));
		QVec<31>::splat(Q<31>::from_raw(-5)).store(back.data(), 3);
		expect(back[0].raw()).to_equal(-5);
		expect(back[2].raw()).to_equal(-5);
		expect(back[3].raw()).to_equal(99);
		std::array<Q<31>, 4> two;
		two.fill(Q<31>::from_raw(99));
		QVec<31>::splat(Q<31>::from_raw(3)).store(two.data(), 2);
		expect(two[1].raw()).to_equal(3);
		expect(two[2].raw()).to_equal(99);
	});
	it("splat a lane into every lane", _ {
		const auto x = Q16x2Vec<15>::splat(Q16x2<15>::from_raw(0x8000'7fffu));
		expect(x.lane(2).raw()).to_equal(0x8000'7fffu);
	});
});
// clang-format on

CPPSPEC_MAIN(fixed_point_neon);
