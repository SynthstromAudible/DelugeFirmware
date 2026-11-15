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

#include "support/matchers/value_matchers.h"

#include "cppspec.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <source_location>
#include <span>
#include <string>

using namespace CppSpec;
using deluge::test::matchers::BeAtLeast;
using deluge::test::matchers::BeAtMost;
using deluge::test::matchers::EqualBytes;

namespace {

/// The message a matcher reports when it fails.
template <typename Matcher>
std::string failure(Matcher matcher) {
	return matcher.run().get_message();
}

} // namespace

// clang-format off
describe value_matchers("the support value matchers", $ {
	context("to_be_at_least", _ {
		it("passes at and above the bound, and under not_() below it", _ {
			to_be_at_least(expect(5), 5);
			to_be_at_least(expect(6), 5);
			to_be_at_least(expect(4).not_(), 5);
		});
		it("fails below the bound, printing both values", _ {
			auto ex = ExpectationValue(4, std::source_location::current());
			expect(failure(BeAtLeast<int, int>(ex, 5))).to_equal(std::string("expected 4 to be at least 5"));
		});
		it("fails on NaN, which no bound admits", _ {
			auto ex = ExpectationValue(std::nan(""), std::source_location::current());
			expect(BeAtLeast<double, double>(ex, 0.0).run().is_failure()).to_be_true();
		});
		it("fails on NaN under not_() too", _ {
			auto ex = ExpectationValue(std::nan(""), std::source_location::current());
			ex.not_();
			expect(BeAtLeast<double, double>(ex, 0.0).run().is_failure()).to_be_true();
		});
		it("compares signed and unsigned integers by value", _ {
			to_be_at_least(expect(3u), -1);
			to_be_at_least(expect(-1).not_(), 0u);
		});
		it("puts a custom message above both values", _ {
			auto ex = ExpectationValue(4, std::source_location::current());
			BeAtLeast<int, int> matcher(ex, 5);
			matcher.set_message("the level");
			expect(failure(matcher)).to_equal(std::string("the level\nexpected 4 to be at least 5"));
		});
	});

	context("to_be_at_most", _ {
		it("passes at and below the bound, and under not_() above it", _ {
			to_be_at_most(expect(5), 5);
			to_be_at_most(expect(4), 5);
			to_be_at_most(expect(6).not_(), 5);
		});
		it("fails above the bound, printing both values", _ {
			auto ex = ExpectationValue(6, std::source_location::current());
			expect(failure(BeAtMost<int, int>(ex, 5))).to_equal(std::string("expected 6 to be at most 5"));
		});
		it("fails on NaN, which no bound admits", _ {
			auto ex = ExpectationValue(std::nan(""), std::source_location::current());
			expect(BeAtMost<double, double>(ex, 0.0).run().is_failure()).to_be_true();
		});
		it("fails on NaN under not_() too", _ {
			auto ex = ExpectationValue(std::nan(""), std::source_location::current());
			ex.not_();
			expect(BeAtMost<double, double>(ex, 0.0).run().is_failure()).to_be_true();
		});
		it("compares signed and unsigned integers by value", _ {
			to_be_at_most(expect(-1), 0u);
			to_be_at_most(expect(3u).not_(), -1);
		});
	});

	context("to_equal_bytes", _ {
		it("passes on equal buffers, and under not_() on different ones", _ {
			const std::array<uint8_t, 3> a{1, 2, 3};
			const std::array<uint8_t, 3> b{1, 9, 3};
			to_equal_bytes(expect(std::span<const uint8_t>(a)), std::span<const uint8_t>(a));
			to_equal_bytes(expect(std::span<const uint8_t>(b)).not_(), std::span<const uint8_t>(a));
		});
		it("names the first byte that differs", _ {
			const std::array<uint8_t, 3> got{1, 9, 3};
			const std::array<uint8_t, 3> want{1, 2, 3};
			auto ex = ExpectationValue(std::span<const uint8_t>(got), std::source_location::current());
			expect(failure(EqualBytes(ex, std::span<const uint8_t>(want))))
			    .to_equal(std::string("expected byte 1 to be 0x02, got 0x09 (3 bytes, 3 expected)"));
		});
		it("names a difference in length", _ {
			const std::array<uint8_t, 2> got{1, 2};
			const std::array<uint8_t, 3> want{1, 2, 3};
			auto ex = ExpectationValue(std::span<const uint8_t>(got), std::source_location::current());
			expect(failure(EqualBytes(ex, std::span<const uint8_t>(want))))
			    .to_equal(std::string("expected 3 bytes, got 2 (the first 2 equal)"));
		});
		it("counts the bytes that should have differed", _ {
			const std::array<uint8_t, 3> a{1, 2, 3};
			auto ex = ExpectationValue(std::span<const uint8_t>(a), std::source_location::current());
			ex.not_();
			expect(failure(EqualBytes(ex, std::span<const uint8_t>(a))))
			    .to_equal(std::string("expected the 3 bytes to differ from the expected ones"));
		});
	});
});
// clang-format on

int value_matchers_spec(int argc, char** const argv) {
	return CppSpec::parse(argc, argv).add_spec(value_matchers).exec().is_success() ? EXIT_SUCCESS : EXIT_FAILURE;
}
