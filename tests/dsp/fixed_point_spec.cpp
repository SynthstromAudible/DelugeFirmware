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
#include <cppspec.hpp>
#include <cstdint>
#include <type_traits>

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
	});
	it("orders values within one format", _ {
		expect(Q<31>::from_raw(-1) < Q<31>::from_raw(0)).to_be_true();
		expect(Q<31>::from_raw(5) == Q<31>::from_raw(5)).to_be_true();
	});
	it("packs and unpacks int16 pairs", _ {
		const auto pair = Q16x2<15>::pack(Q16<15>::from_raw(-2), Q16<15>::from_raw(3));
		expect(pair.raw()).to_equal(0x0003'FFFEu);
		expect(pair.lo().raw()).to_equal(static_cast<std::int16_t>(-2));
		expect(pair.hi().raw()).to_equal(static_cast<std::int16_t>(3));
	});
});
// clang-format on

CPPSPEC_MAIN(fixed_point_types);
