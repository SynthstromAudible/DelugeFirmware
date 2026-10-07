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

// DryWetFade: the fade a filter runs after being switched on, from dry to wet over about 500 samples. The exact step
// count pins its float arithmetic.

#include "dsp-next/fixed/filter/dry_wet_fade.h"

#include <cppspec.hpp>
#include <cstdint>
#include <vector>

using deluge::dsp::fixed::DryWetFade;

// clang-format off
describe dry_wet_fade("DryWetFade", ${
	it("starts mid-fade from its initializers", _ {
		const DryWetFade fade;
		expect(fade.active()).to_be_true();
	});
	it("runs no fade after zero()", _ {
		DryWetFade fade;
		fade.zero();
		expect(fade.active()).to_be_false();
	});
	it("fades again after begin()", _ {
		DryWetFade fade;
		fade.zero();
		fade.begin();
		expect(fade.active()).to_be_true();
	});
	it("ends the fade after 688 samples, once dry falls below 0.001", _ {
		DryWetFade fade;
		fade.zero();
		fade.begin();
		std::vector<int32_t> sample{0};
		std::vector<int32_t> scratch{0};
		int32_t steps = 0;
		while (fade.active()) {
			fade.blend(sample, scratch);
			steps++;
		}
		expect(steps).to_equal(688);
	});
	it("keeps a copy of the dry block", _ {
		const DryWetFade fade;
		const std::vector<int32_t> block{1, -2, 3};
		std::vector<int32_t> scratch(4, 9);
		fade.keepDry(block, scratch);
		expect(scratch).to_equal(std::vector<int32_t>{1, -2, 3, 9});
	});
});
// clang-format on

CPPSPEC_MAIN(dry_wet_fade);
