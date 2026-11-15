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

#pragma once

#include "cppspec.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

/// @file
/// @brief Value-printing matchers CppSpec does not have: an inclusive bound on either side and byte-buffer equality.
///
/// Each has an entry point in namespace `CppSpec`, where argument-dependent lookup finds it from any `expect(...)`:
/// `to_be_at_least(expect(lit), 3);`, `to_equal_bytes(expect(std::span<const uint8_t>(a)), b);`. A failure prints
/// the values compared, which `expect(lit >= 3).to_be_true()` cannot. Text containment is CppSpec's own
/// `expect(s).to_contain(needle)`, which finds a substring when both are text.
namespace deluge::test::matchers {

namespace detail {

/// @brief The integer types `std::cmp_less` and its kin accept: every integral type but `bool` and the character
///        types (`int8_t` and `uint8_t` are in).
template <typename T>
concept cmp_integer =
    std::integral<T> && !std::same_as<std::remove_cv_t<T>, bool> && !std::same_as<std::remove_cv_t<T>, char>
    && !std::same_as<std::remove_cv_t<T>, wchar_t> && !std::same_as<std::remove_cv_t<T>, char8_t>
    && !std::same_as<std::remove_cv_t<T>, char16_t> && !std::same_as<std::remove_cv_t<T>, char32_t>;

/// @brief `a < b`, by value when both are integers: a signed side is not converted to unsigned.
template <typename A, typename E>
constexpr bool less(const A& a, const E& b) {
	if constexpr (cmp_integer<A> && cmp_integer<E>) {
		return std::cmp_less(a, b);
	}
	else {
		return a < b;
	}
}

/// @brief `a <= b`, by value when both are integers.
template <typename A, typename E>
constexpr bool less_equal(const A& a, const E& b) {
	if constexpr (cmp_integer<A> && cmp_integer<E>) {
		return std::cmp_less_equal(a, b);
	}
	else {
		return a <= b;
	}
}

/// @brief `a >= b`, by value when both are integers.
template <typename A, typename E>
constexpr bool greater_equal(const A& a, const E& b) {
	if constexpr (cmp_integer<A> && cmp_integer<E>) {
		return std::cmp_greater_equal(a, b);
	}
	else {
		return a >= b;
	}
}

} // namespace detail

/// @brief Matches a value no smaller than the bound: `actual >= bound`, compared by value when both are integers
///        (`-1` is below `0u`). Under `not_()` it matches `actual < bound`, so NaN, which neither holds for, fails both
///        polarities.
template <typename A, typename E>
class BeAtLeast : public CppSpec::Matchers::MatcherBase<A, E> {
public:
	BeAtLeast(CppSpec::Expectation<A>& expectation, E bound)
	    : CppSpec::Matchers::MatcherBase<A, E>(expectation, std::move(bound)) {}

	std::string verb() override { return "be at least"; }
	bool match() override { return detail::greater_equal(this->actual(), this->expected()); }
	bool negated_match() override { return detail::less(this->actual(), this->expected()); }
};

/// @brief Matches a value no larger than the bound: `actual <= bound`, compared by value when both are integers.
///        Under `not_()` it matches `actual > bound`, so NaN fails both polarities.
template <typename A, typename E>
class BeAtMost : public CppSpec::Matchers::MatcherBase<A, E> {
public:
	BeAtMost(CppSpec::Expectation<A>& expectation, E bound)
	    : CppSpec::Matchers::MatcherBase<A, E>(expectation, std::move(bound)) {}

	std::string verb() override { return "be at most"; }
	bool match() override { return detail::less_equal(this->actual(), this->expected()); }
	bool negated_match() override { return detail::less(this->expected(), this->actual()); }
};

/// @brief Matches a byte buffer equal to the expected one, naming the first byte that differs rather than printing
///        both buffers whole.
class EqualBytes : public CppSpec::Matchers::MatcherBase<std::span<const uint8_t>, std::span<const uint8_t>> {
public:
	EqualBytes(CppSpec::Expectation<std::span<const uint8_t>>& expectation, std::span<const uint8_t> expected)
	    : MatcherBase(expectation, expected) {}

	std::string verb() override { return "equal the bytes"; }
	std::string description() override { return std::format("equal the {} expected bytes", expected().size()); }
	std::string failure_message() override {
		const std::span<const uint8_t> got = actual();
		const std::span<const uint8_t> want = expected();
		const auto [g, w] = std::ranges::mismatch(got, want);
		if (g == got.end() || w == want.end()) {
			return std::format("expected {} bytes, got {} (the first {} equal)", want.size(), got.size(),
			                   std::min(got.size(), want.size()));
		}
		return std::format("expected byte {} to be 0x{:02x}, got 0x{:02x} ({} bytes, {} expected)", g - got.begin(), *w,
		                   *g, got.size(), want.size());
	}
	std::string failure_message_when_negated() override {
		return std::format("expected the {} bytes to differ from the expected ones", actual().size());
	}
	bool match() override { return std::ranges::equal(actual(), expected()); }
};

} // namespace deluge::test::matchers

namespace CppSpec {

/// @brief `to_be_at_least(expect(a), bound)`: @p a is no smaller than @p bound; a failure prints both.
template <typename A, typename E>
void to_be_at_least(Expectation<A>& expectation, E bound, std::string msg = "") {
	deluge::test::matchers::BeAtLeast<A, E>(expectation, std::move(bound)).set_message(std::move(msg)).run();
}
template <typename A, typename E>
void to_be_at_least(Expectation<A>&& expectation, E bound, std::string msg = "") {
	to_be_at_least(expectation, std::move(bound), std::move(msg));
}

/// @brief `to_be_at_most(expect(a), bound)`: @p a is no larger than @p bound; a failure prints both.
template <typename A, typename E>
void to_be_at_most(Expectation<A>& expectation, E bound, std::string msg = "") {
	deluge::test::matchers::BeAtMost<A, E>(expectation, std::move(bound)).set_message(std::move(msg)).run();
}
template <typename A, typename E>
void to_be_at_most(Expectation<A>&& expectation, E bound, std::string msg = "") {
	to_be_at_most(expectation, std::move(bound), std::move(msg));
}

/// @brief `to_equal_bytes(expect(std::span<const uint8_t>(a)), b)`: the buffers are equal; a failure names the
///        first byte that differs.
inline void to_equal_bytes(Expectation<std::span<const uint8_t>>& expectation, std::span<const uint8_t> expected,
                           std::string msg = "") {
	deluge::test::matchers::EqualBytes(expectation, expected).set_message(std::move(msg)).run();
}
inline void to_equal_bytes(Expectation<std::span<const uint8_t>>&& expectation, std::span<const uint8_t> expected,
                           std::string msg = "") {
	to_equal_bytes(expectation, expected, std::move(msg));
}

} // namespace CppSpec
