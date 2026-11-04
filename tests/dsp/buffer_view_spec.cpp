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
#include "dsp-next/core/buffer_view.h"
#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <span>
#include <type_traits>

using namespace deluge::dsp;

namespace {
static_assert(std::is_same_v<decltype(typed_view(std::span<std::int32_t>{})), std::span<Q<31>>>);
static_assert(std::is_same_v<decltype(typed_view(std::span<const std::int32_t>{})), std::span<const Q<31>>>);
static_assert(std::is_same_v<decltype(raw_view(std::span<Q<31>>{})), std::span<std::int32_t>>);
static_assert(std::is_same_v<decltype(raw_view(std::span<const Q<31>>{})), std::span<const std::int32_t>>);
} // namespace

// clang-format off
describe buffer_view("the raw seam view", ${
	it("reads raw samples as Q31 values, edges included", _ {
		std::array<std::int32_t, 4> raw{INT32_MIN, -1, 0, INT32_MAX};
		const auto typed = typed_view(std::span{raw});
		expect(typed.size()).to_equal(std::size_t{4});
		expect(typed[0] == Q<31>::min()).to_be_true();
		expect(typed[1].raw()).to_equal(-1);
		expect(typed[2].raw()).to_equal(0);
		expect(typed[3] == Q<31>::max()).to_be_true();
	});
	it("writes through to the raw samples", _ {
		std::array<std::int32_t, 2> raw{};
		typed_view(std::span{raw})[1] = Q<31>::from_raw(42);
		expect(raw[1]).to_equal(42);
	});
	it("views the same storage both ways", _ {
		std::array<std::int32_t, 3> raw{1, 2, 3};
		const std::span<std::int32_t> s{raw};
		expect(raw_view(typed_view(s)).data() == s.data()).to_be_true();
		expect(raw_view(typed_view(s)).size()).to_equal(std::size_t{3});
		const std::span<const std::int32_t> c{raw};
		expect(raw_view(typed_view(c)).data() == c.data()).to_be_true();
	});
});
// clang-format on

CPPSPEC_MAIN(buffer_view);
