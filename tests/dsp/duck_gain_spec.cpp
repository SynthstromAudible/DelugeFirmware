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

// duck_gain at both scalings against the two expressions it replaces (the clip chain's and the reverb return's), over
// the duck's whole range and the depths a patch cable's amount gives, negative ones included, and its neutral values.

#include "dsp-next/fixed/dynamics/duck_gain.h"

#include "dsp-next/fixed/noise/noise.h"
#include "reference_effects.h"

#include <array>
#include <cppspec.hpp>
#include <cstdint>
#include <limits>

using namespace deluge::dsp;
using namespace deluge::dsp::fixed;

namespace {

/// The depths compared, Q29: none, the least, the patch cable's range at a quarter, a half and full, and negative.
constexpr std::array<q31, 7> kDepths{0, 1, 1 << 27, 0x20000000, 0x3FFFFFFF, -0x20000000, -1};
/// The ducks compared before the noise: the extremes and zero's neighbours.
constexpr std::array<q31, 5> kEdges{std::numeric_limits<q31>::min(), -1, 0, 1, std::numeric_limits<q31>::max()};
/// Noise ducks per depth.
constexpr int kNoise = 20000;

} // namespace

// clang-format off
describe duck_gain_spec("the duck's squared level at the clip's and the reverb return's scales", ${
	it("is neutral at no duck", _ {
		expect(duck_gain<DuckScale::CLIP>(0, 0x20000000)).to_equal(134217728);
		expect(duck_gain<DuckScale::REVERB_RETURN>(0, 0x20000000)).to_equal(1 << 29);
		expect(duck_gain<DuckScale::REVERB_RETURN>(std::numeric_limits<q31>::min(), 0)).to_equal(1 << 29);
	});

	it("equals both legacy expressions across the duck's range", _ {
		for (const q31 depth : kDepths) {
			for (const q31 duck : kEdges) {
				expect(duck_gain<DuckScale::CLIP>(duck, depth)).to_equal(reference::clip_duck_level(duck, depth));
				expect(duck_gain<DuckScale::REVERB_RETURN>(duck, depth))
				    .to_equal(reference::reverb_duck_level(duck, depth));
			}
			SeededNoise noise{0xD0C40001};
			for (int i = 0; i < kNoise; ++i) {
				const q31 duck = noise.render();
				expect(duck_gain<DuckScale::CLIP>(duck, depth)).to_equal(reference::clip_duck_level(duck, depth));
				expect(duck_gain<DuckScale::REVERB_RETURN>(duck, depth))
				    .to_equal(reference::reverb_duck_level(duck, depth));
			}
		}
	});
});
// clang-format on

CPPSPEC_MAIN(duck_gain_spec);
