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

#include "effects/chain.h"
#include "effects/words.h"

#include <cppspec.hpp>
#include <string_view>
#include <utility>

using namespace deluge::effects;

// clang-format off
describe filter_words("the filter types and families", ${
	it("orders the filter types as the file words name them", _ {
		expect(std::to_underlying(FilterType::LPF)).to_equal(0);
		expect(std::to_underlying(FilterType::HPF)).to_equal(1);
		expect(std::to_underlying(FilterType::EQ)).to_equal(2);
		expect(kFilterTypeWords[std::to_underlying(FilterType::LPF)]).to_equal(std::string_view("lpf"));
		expect(kFilterTypeWords[std::to_underlying(FilterType::HPF)]).to_equal(std::string_view("hpf"));
		expect(kFilterTypeWords[std::to_underlying(FilterType::EQ)]).to_equal(std::string_view("eq"));
		expect(kNumFilterTypes).to_equal(3);
	});
	it("puts the ladder modes in the ladder families and the SVF modes in the SVF family", _ {
		expect(filter_family(FilterMode::TRANSISTOR_12DB)).to_equal(FilterFamily::LP_LADDER);
		expect(filter_family(FilterMode::TRANSISTOR_24DB)).to_equal(FilterFamily::LP_LADDER);
		expect(filter_family(FilterMode::TRANSISTOR_24DB_DRIVE)).to_equal(FilterFamily::LP_LADDER);
		expect(filter_family(FilterMode::SVF_BAND)).to_equal(FilterFamily::SVF);
		expect(filter_family(FilterMode::SVF_NOTCH)).to_equal(FilterFamily::SVF);
		expect(filter_family(FilterMode::HPLADDER)).to_equal(FilterFamily::HP_LADDER);
		expect(filter_family(FilterMode::OFF)).to_equal(FilterFamily::NONE);
	});
	it("orders the families ladder, SVF, high-pass ladder, none", _ {
		expect(std::to_underlying(FilterFamily::LP_LADDER)).to_equal(0);
		expect(std::to_underlying(FilterFamily::SVF)).to_equal(1);
		expect(std::to_underlying(FilterFamily::HP_LADDER)).to_equal(2);
		expect(std::to_underlying(FilterFamily::NONE)).to_equal(3);
	});
});

CPPSPEC_SPEC(filter_words)
// clang-format on
