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
#include "dsp-next/core/ramp.h"
#include <cppspec.hpp>
#include <cstdint>
#include <type_traits>

using deluge::dsp::Q;
using deluge::dsp::Ramp;

static_assert(std::is_same_v<decltype(Ramp{Q<31>{}, Q<31>{}}.value()), Q<31>>);
static_assert(std::is_same_v<decltype(Ramp{Q<31>{}, Q<31>{}}.increment()), Q<31>>);

// clang-format off
describe ramp("deluge::dsp::Ramp", ${
	it("scales value and slope by 2^N, wrapping", _ {
		constexpr Ramp r = deluge::dsp::scale_shl_wrap<1>(Ramp{Q<31>::from_raw(0x4000'0000), Q<31>::from_raw(3)});
		static_assert(r.value().raw() == INT32_MIN);
		expect(r.value().raw()).to_equal(INT32_MIN);
		expect(r.increment().raw()).to_equal(6);
	});
});
// clang-format on

CPPSPEC_MAIN(ramp);
