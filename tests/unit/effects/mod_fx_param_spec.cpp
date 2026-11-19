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

// effects::ModFxParam: its enumerators index kModFxParamWords in the order the file words name them.

#include "effects/chain.h"
#include "effects/words.h"

#include <cppspec.hpp>

#include <string_view>
#include <utility>

using namespace deluge::effects;

// clang-format off
describe mod_fx_param("the mod-FX params", ${
	it("are in the order their file words are", _ {
		expect(kModFxParamWords[std::to_underlying(ModFxParam::DEPTH)]).to_equal(std::string_view{"depth"});
		expect(kModFxParamWords[std::to_underlying(ModFxParam::FEEDBACK)]).to_equal(std::string_view{"feedback"});
		expect(kModFxParamWords[std::to_underlying(ModFxParam::OFFSET)]).to_equal(std::string_view{"offset"});
	});
});

CPPSPEC_SPEC(mod_fx_param)
// clang-format on
