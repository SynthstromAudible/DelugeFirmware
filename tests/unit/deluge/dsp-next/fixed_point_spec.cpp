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
#include "dsp-next/core/fixed_point.h"
#include <algorithm>
#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

using namespace deluge::dsp;

namespace {

template <class T>
constexpr bool layout_ok =
    std::is_trivial_v<T> && std::is_standard_layout_v<T> && sizeof(T) == sizeof(typename T::storage_type)
    && alignof(T) == alignof(typename T::storage_type);
static_assert(layout_ok<Q<31>> && layout_ok<Q<0>> && layout_ok<UQ<32>> && layout_ok<Q16<15>> && layout_ok<Q64<62>>
              && layout_ok<UQ64<64>>);
static_assert(std::is_trivial_v<Q16x2<15>> && sizeof(Q16x2<15>) == 4);

// No arithmetic operators, no implicit conversions, no cross-format comparison.
template <class A, class B>
concept addable = requires(A a, B b) { a + b; };
template <class A, class B>
concept multipliable = requires(A a, B b) { a* b; };
template <class A>
concept shiftable = requires(A a) { a << 1; };
template <class A>
concept negatable = requires(A a) { -a; };
template <class A, class B>
concept comparable = requires(A a, B b) { a == b; };
static_assert(!addable<Q<31>, Q<31>>);
static_assert(!multipliable<Q<31>, Q<31>>);
static_assert(!shiftable<Q<31>>);
static_assert(!negatable<Q<31>>);
static_assert(!std::is_convertible_v<std::int32_t, Q<31>>);
static_assert(!std::is_convertible_v<Q<31>, std::int32_t>);
static_assert(!comparable<Q<31>, Q<30>>);
static_assert(comparable<Q<31>, Q<31>>);

static_assert(std::is_same_v<decltype(mul(Q<31>{}, Q<31>{})), Q<30>>);
static_assert(std::is_same_v<decltype(mul(Q<27>{}, Q<31>{})), Q<26>>);
static_assert(std::is_same_v<decltype(mul_wide(Q<31>{}, Q<31>{})), Q64<62>>);
static_assert(std::is_same_v<decltype(mul_doubling_sat(Q<31>{}, Q<31>{})), Q<31>>);
static_assert(std::is_same_v<decltype(mul(Q<31>{}, Q16<15>{})), Q<30>>);
static_assert(std::is_same_v<decltype(mul(UQ<32>{}, Q<31>{})), Q<31>>);
static_assert(std::is_same_v<decltype(dot(Q16x2<15>{}, Q16x2<15>{})), Q<30>>);
template <class A, class B>
concept high_word_multipliable = requires(A a, B b) { mul(a, b); };
static_assert(!high_word_multipliable<Q<15>, Q<15>>);
template <class To, class From>
concept wrap_reformattable = requires(From x) { as_wrap<To>(x); };
template <class To, class From>
concept floor_reformattable = requires(From x) { as_floor<To>(x); };
template <int N, class T>
concept wrap_scalable = requires(T x) { scale_shl_wrap<N>(x); };
static_assert(wrap_reformattable<Q<31>, Q<30>>);
static_assert(!wrap_reformattable<Q<30>, Q<31>>);
static_assert(!floor_reformattable<Q<31>, Q<30>>);
static_assert(!wrap_reformattable<UQ<31>, Q<30>>);
static_assert(wrap_scalable<31, Q<31>>);
template <class To, class From>
concept round_reformattable = requires(From x) { as_round<To>(x); };
template <class To, class From>
concept widenable = requires(From x) { widen<To>(x); };
// A reformat or widen by the storage's whole width (or more) would shift by it: undefined, so it does not compile.
static_assert(wrap_reformattable<UQ<32>, UQ<1>>);
static_assert(!wrap_reformattable<UQ<32>, UQ<0>>);
static_assert(!floor_reformattable<UQ<0>, UQ<32>>);
static_assert(!round_reformattable<UQ<0>, UQ<32>>);
static_assert(!wrap_reformattable<UQ64<64>, UQ64<0>>);
static_assert(widenable<Q64<32>, Q<0>>);
static_assert(!widenable<Q64<63>, Q<0>>);
static_assert(!wrap_scalable<32, Q<31>>);
static_assert(std::is_same_v<decltype(to_q16_sat(Q<31>{})), Q16<15>>);
static_assert(std::is_same_v<decltype(from_q16(Q16<15>{})), Q<31>>);

constexpr std::array<std::int32_t, 14> kEdges{
    0, 1, -1, 2, -2, INT32_MAX, INT32_MIN, INT32_MAX - 1, INT32_MIN + 1, 1 << 30, -(1 << 30), 0x7FFF, -0x8000, 0x10000};

std::uint32_t xorshift(std::uint32_t& s) {
	s ^= s << 13;
	s ^= s >> 17;
	s ^= s << 5;
	return s;
}

/// The edge set against itself, then 200 000 pseudo-random pairs.
const std::vector<std::pair<std::int32_t, std::int32_t>>& pairs() {
	static const auto out = [] {
		std::vector<std::pair<std::int32_t, std::int32_t>> v;
		for (auto a : kEdges) {
			for (auto b : kEdges) {
				v.emplace_back(a, b);
			}
		}
		std::uint32_t s = 0x2545F491u;
		for (int i = 0; i < 200000; ++i) {
			const auto a = static_cast<std::int32_t>(xorshift(s));
			v.emplace_back(a, static_cast<std::int32_t>(xorshift(s)));
		}
		return v;
	}();
	return out;
}

std::int32_t sat32(std::int64_t v) {
	return v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : static_cast<std::int32_t>(v);
}
std::int32_t wrap32(std::int64_t v) {
	return static_cast<std::int32_t>(static_cast<std::uint32_t>(v));
}
std::int32_t clamp_ref(std::int64_t v, int bits) {
	const std::int64_t hi = (std::int64_t{1} << (bits - 1)) - 1;
	const std::int64_t lo = -hi - 1;
	return static_cast<std::int32_t>(v > hi ? hi : v < lo ? lo : v);
}
Q<31> r(std::int32_t v) {
	return Q<31>::from_raw(v);
}

std::int32_t acc_of(std::int32_t a, std::int32_t b) {
	return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) * 2654435761u ^ static_cast<std::uint32_t>(b));
}
std::int32_t lo16(std::int32_t v) {
	return static_cast<std::int16_t>(static_cast<std::uint32_t>(v) & 0xFFFFu);
}
std::int32_t hi16(std::int32_t v) {
	return static_cast<std::int16_t>(static_cast<std::uint32_t>(v) >> 16);
}

} // namespace

// clang-format off
describe fixed_point_types("fixed_point types", ${
	it("round-trips raw storage", _ {
		expect(Q<31>::from_raw(-123).raw()).to_equal(-123);
		expect(UQ<32>::from_raw(0xFFFF'FFFFu).raw()).to_equal(0xFFFF'FFFFu);
	});
	it("gives one as 2^F, or max where 2^F does not fit", _ {
		expect(Q<30>::one().raw()).to_equal(1 << 30);
		expect(Q<27>::one().raw()).to_equal(1 << 27);
		expect(Q<31>::one().raw()).to_equal(INT32_MAX);
		expect(UQ<32>::one().raw()).to_equal(UINT32_MAX);
		expect(Q16<15>::one().raw()).to_equal(static_cast<std::int16_t>(INT16_MAX));
	});
	it("builds constants at compile time, rounding to nearest", _ {
		constexpr auto half = q<31>(0.5);
		expect(half.raw()).to_equal(1 << 30);
		constexpr auto minus_quarter = q<30>(-0.25);
		expect(minus_quarter.raw()).to_equal(-(1 << 28));
		constexpr auto lsb_and_a_half = q<0>(1.5);
		expect(lsb_and_a_half.raw()).to_equal(2);
		constexpr auto minus_one = q<31>(-1.0);
		expect(minus_one.raw()).to_equal(INT32_MIN);
		// Rounded once: adding 0.5 first would round the double just below 0.5 up to 1.
		constexpr auto just_below_half = q<0>(0.49999999999999994);
		expect(just_below_half.raw()).to_equal(0);
		constexpr auto minus_two_and_a_half = q<0>(-2.5);
		expect(minus_two_and_a_half.raw()).to_equal(-3);
		constexpr auto max_q16 = q16<0>(32767.0);
		expect(max_q16.raw()).to_equal(static_cast<std::int16_t>(32767));
	});
	it("orders values within one format", _ {
		const auto lo = Q<31>::from_raw(-1);
		const auto hi = Q<31>::from_raw(0);
		expect(lo < hi).to_be_true();
		expect(lo <= hi).to_be_true();
		expect(hi > lo).to_be_true();
		expect(hi >= lo).to_be_true();
		expect(lo != hi).to_be_true();
		expect(hi < lo).to_be_false();
		expect(Q<31>::from_raw(5) == Q<31>::from_raw(5)).to_be_true();
		expect(UQ<32>::from_raw(0x8000'0000u) > UQ<32>::from_raw(1u)).to_be_true();
		expect(Q16x2<15>::from_raw(7u) != Q16x2<15>::from_raw(8u)).to_be_true();
	});
	it("packs and unpacks int16 pairs", _ {
		const auto pair = Q16x2<15>::pack(Q16<15>::from_raw(-2), Q16<15>::from_raw(3));
		expect(pair.raw()).to_equal(0x0003'FFFEu);
		expect(pair.lo().raw()).to_equal(static_cast<std::int16_t>(-2));
		expect(pair.hi().raw()).to_equal(static_cast<std::int16_t>(3));
	});
});
// clang-format on

// clang-format off
describe fixed_point_add("fixed_point add/sub/clamp", ${
	it("wraps modulo 2^32 and saturates to the int32 range", _ {
		std::size_t bad = 0;
		for (auto [a, b] : pairs()) {
			const std::int64_t sum = std::int64_t{a} + b;
			const std::int64_t diff = std::int64_t{a} - b;
			bad += add_wrap(r(a), r(b)).raw() != wrap32(sum);
			bad += sub_wrap(r(a), r(b)).raw() != wrap32(diff);
			bad += add_sat(r(a), r(b)).raw() != sat32(sum);
			bad += sub_sat(r(a), r(b)).raw() != sat32(diff);
			bad += add_double_sat(r(a), r(b)).raw() != sat32(std::int64_t{a} + sat32(std::int64_t{b} * 2));
			bad += sub_double_sat(r(a), r(b)).raw() != sat32(std::int64_t{a} - sat32(std::int64_t{b} * 2));
			bad += add_halves_sat(r(a), r(b)).raw() != wrap32(std::int64_t{clamp_ref((a >> 1) + (b >> 1), 31)} * 2);
		}
		expect(bad).to_equal(std::size_t{0});
	});
	it("negates with wrap and with saturation", _ {
		expect(neg_wrap(r(INT32_MIN)).raw()).to_equal(INT32_MIN);
		expect(neg_sat(r(INT32_MIN)).raw()).to_equal(INT32_MAX);
		expect(neg_sat(r(5)).raw()).to_equal(-5);
		expect(neg_wrap(UQ<32>::from_raw(1u)).raw()).to_equal(UINT32_MAX);
	});
	it("clamps to N signed or unsigned bits", _ {
		std::size_t bad = 0;
		for (auto [a, b] : pairs()) {
			bad += clamp_bits<24>(r(a)).raw() != clamp_ref(a, 24);
			bad += clamp_bits<1>(r(a)).raw() != clamp_ref(a, 1);
			bad += clamp_bits<32>(r(a)).raw() != a;
			const int n = 1 + static_cast<int>(static_cast<std::uint32_t>(b) % 32u);
			bad += clamp_bits(r(a), n).raw() != clamp_ref(a, n);
			constexpr std::int64_t kUnsignedHi = (std::int64_t{1} << 16) - 1;
			bad += clamp_unsigned_bits<16>(r(a)).raw()
			       != static_cast<std::int32_t>(a < 0 ? 0 : a > kUnsignedHi ? kUnsignedHi : a);
			bad += clamp_high(r(a), r(b)).raw() != (a > b ? b : a);
		}
		expect(bad).to_equal(std::size_t{0});
	});
	it("works lane-wise on packed int16 pairs", _ {
		const auto x = Q16x2<15>::pack(Q16<15>::from_raw(30000), Q16<15>::from_raw(-30000));
		const auto y = Q16x2<15>::pack(Q16<15>::from_raw(10000), Q16<15>::from_raw(-10000));
		expect(add_sat(x, y).lo().raw()).to_equal(static_cast<std::int16_t>(32767));
		expect(add_sat(x, y).hi().raw()).to_equal(static_cast<std::int16_t>(-32768));
		expect(sub_sat(x, y).lo().raw()).to_equal(static_cast<std::int16_t>(20000));
		expect(sub_sat(y, x).hi().raw()).to_equal(static_cast<std::int16_t>(20000));
		expect(halving_add(x, y).lo().raw()).to_equal(static_cast<std::int16_t>(20000));
		expect(halving_add(x, y).hi().raw()).to_equal(static_cast<std::int16_t>(-20000));
	});
	it("evaluates at compile time", _ {
		constexpr auto c = add_sat(Q<31>::max(), Q<31>::one());
		expect(c.raw()).to_equal(INT32_MAX);
	});
});
// clang-format on

// clang-format off
describe fixed_point_mul("fixed_point multiplies", ${
	it("takes the high word of the product, truncated and rounded", _ {
		std::size_t bad = 0;
		for (auto [a, b] : pairs()) {
			const std::int64_t p = std::int64_t{a} * b;
			const std::int32_t c = acc_of(a, b);
			const auto acc = Q<30>::from_raw(c);
			bad += mul(r(a), r(b)).raw() != static_cast<std::int32_t>(p >> 32);
			bad += mul_r(r(a), r(b)).raw() != static_cast<std::int32_t>((p + 0x80000000LL) >> 32);
			bad += mla(acc, r(a), r(b)).raw() != wrap32(std::int64_t{c} + (p >> 32));
			bad += mla_r(acc, r(a), r(b)).raw() != wrap32(std::int64_t{c} + ((p + 0x80000000LL) >> 32));
			bad += mls(acc, r(a), r(b)).raw() != wrap32(std::int64_t{c} + ((-p) >> 32));
			bad += mls_r(acc, r(a), r(b)).raw() != wrap32(std::int64_t{c} + ((0x80000000LL - p) >> 32));
			bad += mul_wide(r(a), r(b)).raw() != p;
			bad += mla_wide(Q64<62>::from_raw(p), r(a), r(b)).raw()
			       != static_cast<std::int64_t>(static_cast<std::uint64_t>(p) * 2);
		}
		expect(bad).to_equal(std::size_t{0});
	});
	it("subtracts the ceiling of the product in mls, not the truncated mul", _ {
		const auto zero = Q<30>::from_raw(0);
		expect(mls(zero, r(1), r(1)).raw()).to_equal(-1);
		expect(sub_wrap(zero, mul(r(1), r(1))).raw()).to_equal(0);
	});
	it("multiplies unsigned and mixed-sign operands exactly", _ {
		std::size_t bad = 0;
		for (auto [a, b] : pairs()) {
			const auto ua = static_cast<std::uint32_t>(a);
			const auto ub = static_cast<std::uint32_t>(b);
			bad += mul(UQ<32>::from_raw(ua), UQ<32>::from_raw(ub)).raw()
			       != static_cast<std::uint32_t>((std::uint64_t{ua} * ub) >> 32);
			bad += mul(UQ<32>::from_raw(ua), r(b)).raw()
			       != static_cast<std::int32_t>((static_cast<std::int64_t>(ua) * b) >> 32);
		}
		expect(bad).to_equal(std::size_t{0});
	});
	it("doubles the product and saturates as vqdmulh and vqrdmulh do", _ {
		std::size_t bad = 0;
		for (auto [a, b] : pairs()) {
			const std::int64_t p = std::int64_t{a} * b;
			const bool both_min = a == INT32_MIN && b == INT32_MIN;
			bad += mul_doubling_sat(r(a), r(b)).raw() != (both_min ? INT32_MAX : static_cast<std::int32_t>(p >> 31));
			bad += mul_doubling_r_sat(r(a), r(b)).raw()
			       != (both_min ? INT32_MAX : static_cast<std::int32_t>((p + (1LL << 30)) >> 31));
		}
		expect(bad).to_equal(std::size_t{0});
		// The doubling multiply keeps a bit the high word drops: 3 × 0.5 in Q31.
		expect(mul_doubling_sat(r(3), r(1 << 30)).raw()).to_equal(1);
		expect(mul(r(3), r(1 << 30)).raw()).to_equal(0);
	});
	it("multiplies by 16-bit values and packed pairs as the SIMD32 multiplies do", _ {
		std::size_t bad = 0;
		for (auto [a, b] : pairs()) {
			const std::int32_t c = acc_of(a, b);
			const auto pa = Q16x2<15>::from_raw(static_cast<std::uint32_t>(a));
			const auto pb = Q16x2<15>::from_raw(static_cast<std::uint32_t>(b));
			const auto b16 = Q16<15>::from_raw(static_cast<std::int16_t>(lo16(b)));
			bad += mul(r(a), b16).raw() != static_cast<std::int32_t>((std::int64_t{a} * lo16(b)) >> 16);
			bad += mla(Q<30>::from_raw(c), r(a), b16).raw()
			       != wrap32(std::int64_t{c} + ((std::int64_t{a} * lo16(b)) >> 16));
			bad += mul_bottom(r(a), pb).raw() != static_cast<std::int32_t>((std::int64_t{a} * lo16(b)) >> 16);
			bad += mul_top(r(a), pb).raw() != static_cast<std::int32_t>((std::int64_t{a} * hi16(b)) >> 16);
			const std::int64_t lolo = std::int64_t{lo16(a)} * lo16(b);
			const std::int64_t hihi = std::int64_t{hi16(a)} * hi16(b);
			bad += dot(pa, pb).raw() != wrap32(lolo + hihi);
			bad += dot_diff(pa, pb).raw() != wrap32(lolo - hihi);
			bad += dot_acc(Q<30>::from_raw(c), pa, pb).raw() != wrap32(std::int64_t{c} + lolo + hihi);
			bad += dot_wide_acc(Q64<30>::from_raw(std::int64_t{c} << 20), pa, pb).raw()
			       != (std::int64_t{c} << 20) + lolo + hihi;
		}
		expect(bad).to_equal(std::size_t{0});
	});
	it("reinterprets bits only on request", _ {
		expect(reinterpret_bits<Q<31>>(UQ<32>::from_raw(0x8000'0000u)).raw()).to_equal(INT32_MIN);
	});
	it("evaluates at compile time", _ {
		constexpr auto c = mul(Q<31>::from_raw(1 << 30), Q<31>::from_raw(1 << 30));
		expect(c.raw()).to_equal(1 << 28);
	});
});
// clang-format on

// clang-format off
describe fixed_point_format("fixed_point reformat and scale", ${
	it("widens wrapping, saturating, or clamping then shifting", _ {
		std::size_t bad = 0;
		for (auto [a, b] : pairs()) {
			const auto x = Q<28>::from_raw(a);
			bad += as_wrap<Q<31>>(x).raw() != wrap32(std::int64_t{a} * 8);
			bad += as_sat<Q<31>>(x).raw() != sat32(std::int64_t{a} * 8);
			bad += as_sat<Q<31>>(Q<30>::from_raw(a)).raw() != sat32(std::int64_t{a} * 2);
			bad += as_clamp_then_shift<Q<31>>(x).raw() != wrap32(std::int64_t{clamp_ref(a, 29)} * 8);
			bad += scale_shl_wrap<3>(r(a)).raw() != wrap32(std::int64_t{a} * 8);
			bad += scale_shl_sat<3>(r(a)).raw() != sat32(std::int64_t{a} * 8);
			bad += scale_shl_clamp_then_shift<3>(r(a)).raw() != wrap32(std::int64_t{clamp_ref(a, 29)} * 8);
		}
		expect(bad).to_equal(std::size_t{0});
		expect(as_sat<Q<31>>(Q<30>::max()).raw()).to_equal(INT32_MAX);
		expect(as_clamp_then_shift<Q<31>>(Q<30>::max()).raw()).to_equal(INT32_MAX - 1);
	});
	it("narrows by flooring or rounding half up, never wrapping", _ {
		std::size_t bad = 0;
		for (auto [a, b] : pairs()) {
			const std::int64_t round_ref = (std::int64_t{a} + 4) >> 3;
			bad += as_floor<Q<28>>(r(a)).raw() != (a >> 3);
			bad += as_round<Q<28>>(r(a)).raw() != static_cast<std::int32_t>(round_ref);
			bad += scale_shr_floor<3>(r(a)).raw() != (a >> 3);
			bad += scale_shr_round<3>(r(a)).raw() != static_cast<std::int32_t>(round_ref);
			const auto u = static_cast<std::uint32_t>(a);
			bad += as_round<UQ<29>>(UQ<32>::from_raw(u)).raw() != static_cast<std::uint32_t>((std::uint64_t{u} + 4) >> 3);
		}
		expect(bad).to_equal(std::size_t{0});
		expect(as_round<Q<30>>(Q<31>::max()).raw()).to_equal(1 << 30);
	});
	it("halves by flooring or toward zero", _ {
		expect(halve_floor(r(-3)).raw()).to_equal(-2);
		expect(halve_toward_zero(r(-3)).raw()).to_equal(-1);
		expect(halve_floor(r(3)).raw()).to_equal(1);
	});
	it("changes width between 16, 32 and 64 bits", _ {
		std::size_t bad = 0;
		for (auto [a, b] : pairs()) {
			const std::int64_t wide = std::int64_t{a} * b;
			bad += widen<Q64<40>>(Q<31>::from_raw(a)).raw() != std::int64_t{a} * 512;
			bad += narrow_floor_wrap<Q<31>>(Q64<62>::from_raw(wide)).raw() != wrap32(wide >> 31);
			bad += narrow_round_wrap<Q<31>>(Q64<62>::from_raw(wide)).raw() != wrap32((wide + (1LL << 30)) >> 31);
			bad += narrow_floor_sat<Q<31>>(Q64<62>::from_raw(wide)).raw() != sat32(wide >> 31);
			bad += to_q16_sat(r(a)).raw()
			       != static_cast<std::int16_t>(std::clamp<std::int32_t>(a >> 16, INT16_MIN, INT16_MAX));
			bad += from_q16(Q16<15>::from_raw(static_cast<std::int16_t>(lo16(a)))).raw() != lo16(a) * 65536;
		}
		expect(bad).to_equal(std::size_t{0});
	});
	it("composes a multiply with a reformat", _ {
		std::size_t bad = 0;
		for (auto [a, b] : pairs()) {
			const std::int64_t p = std::int64_t{a} * b;
			bad += mul_as_wrap<Q<31>>(r(a), r(b)).raw() != wrap32((p >> 32) * 2);
			bad += mul_as_sat<Q<31>>(r(a), r(b)).raw() != sat32((p >> 32) * 2);
			bad += mul_r_as_wrap<Q<31>>(r(a), r(b)).raw() != wrap32(((p + 0x80000000LL) >> 32) * 2);
		}
		expect(bad).to_equal(std::size_t{0});
		expect(mul_as_wrap<Q<31>>(r(INT32_MIN), r(INT32_MIN)).raw()).to_equal(INT32_MIN);
		// The saturated reformat of the high word drops the bit the doubling multiply keeps.
		expect(as_sat<Q<31>>(mul(r(3), r(1 << 30))).raw()).to_equal(0);
	});
});
// clang-format on

// clang-format off
describe fixed_point_float("fixed_point float conversions", ${
	it("converts from float as VFP vcvt does: truncating, saturating, NaN to zero", _ {
		constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
		expect(from_float<0>(kNaN).raw()).to_equal(0);
		expect(from_float<16>(kNaN).raw()).to_equal(0);
		expect(from_float<31>(kNaN).raw()).to_equal(0);
		expect(from_float<31>(1.0f).raw()).to_equal(INT32_MAX);
		expect(from_float<31>(-1.0f).raw()).to_equal(INT32_MIN);
		expect(from_float<31>(0.75f).raw()).to_equal(0x6000'0000);
		expect(from_float<31>(std::numeric_limits<float>::infinity()).raw()).to_equal(INT32_MAX);
		expect(from_float<0>(-1.9f).raw()).to_equal(-1);
		expect(from_float<24>(-2.5f).raw()).to_equal(-(5 << 23));
		expect(from_float<1>(1e20f).raw()).to_equal(INT32_MAX);
	});
	it("converts to float exactly where the value fits", _ {
		expect(to_float(Q<31>::from_raw(1 << 30))).to_equal(0.5f);
		expect(to_float(Q<0>::from_raw(-7))).to_equal(-7.0f);
		expect(to_float(Q<27>::one())).to_equal(1.0f);
		expect(to_float(Q<31>::min())).to_equal(-1.0f);
	});
	it("rounds half away from zero when asked", _ {
		expect(from_float_round<0>(2.5f).raw()).to_equal(3);
		expect(from_float_round<0>(-2.5f).raw()).to_equal(-3);
		expect(from_float_round<0>(2.4f).raw()).to_equal(2);
		expect(from_float_round<31>(std::numeric_limits<float>::quiet_NaN()).raw()).to_equal(0);
		expect(from_float_round<31>(2.0f).raw()).to_equal(INT32_MAX);
		expect(from_float_round<31>(-2.0f).raw()).to_equal(INT32_MIN);
	});
	it("converts at compile time", _ {
		constexpr auto c = from_float<31>(0.5f);
		expect(c.raw()).to_equal(1 << 30);
	});
});
// clang-format on

CPPSPEC_MAIN(fixed_point_types, fixed_point_add, fixed_point_mul, fixed_point_format, fixed_point_float);
