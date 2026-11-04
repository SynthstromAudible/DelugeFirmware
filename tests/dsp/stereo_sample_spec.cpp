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
#include "dsp-next/core/stereo_sample.h"
#include <cppspec.hpp>
#include <cstdint>

using deluge::dsp::Q;
using deluge::dsp::StereoSample;

// A sum that leaves Q31 wraps with defined behaviour, so it is a constant expression.
static_assert((StereoSample{.l = Q<31>::from_raw(INT32_MAX)} + StereoSample{.l = Q<31>::from_raw(1)}).l.raw()
              == INT32_MIN);
static_assert([] {
	StereoSample s{.r = Q<31>::from_raw(INT32_MIN)};
	s += StereoSample{.r = Q<31>::from_raw(-1)};
	return s.r.raw();
}() == INT32_MAX);

// clang-format off
describe stereo_sample("deluge::dsp::StereoSample", ${
	it("pans a mono sample by Q30 amplitudes, unity at 2^30", _ {
		for (const int32_t mono : {0, 1, -1, 12345, -12345, 0x1234'5678, INT32_MIN, INT32_MAX}) {
			const StereoSample s = deluge::dsp::panned_mono(Q<31>::from_raw(mono), Q<30>::one(), Q<30>::one());
			expect(s.l.raw()).to_equal(mono & ~3);
			expect(s.r.raw()).to_equal(mono & ~3);
		}
	});
	it("pans each channel by its own amplitude", _ {
		const StereoSample s = deluge::dsp::panned_stereo(Q<31>::from_raw(1 << 30), Q<31>::from_raw(-(1 << 30)),
		                                                  Q<30>::from_raw(1 << 29), Q<30>::one());
		expect(s.l.raw()).to_equal(1 << 29);
		expect(s.r.raw()).to_equal(-(1 << 30));
	});
});
// clang-format on

CPPSPEC_MAIN(stereo_sample);
