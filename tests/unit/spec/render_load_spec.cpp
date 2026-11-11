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
// The render's measured cost per frame, smoothed, as the time one 128-frame block would take: what the CPU cull and
// direness decide on, whatever window sizes the board renders and however many renders a tick holds.

#include "processing/engines/render_load.h"

#include "cppspec.hpp"

using deluge::audio::RenderLoad;

namespace {
constexpr double kRate = 44100.0;
} // namespace

// clang-format off
describe render_load("deluge::audio::RenderLoad", ${
	it("is 0 before any render", _ {
		RenderLoad load;
		expect(load.block_samples()).to_equal(0);
	});

	it("reads a full block rendered in real time as the whole block", _ {
		RenderLoad load;
		load.add(128 / kRate, 128);
		expect(load.block_samples()).to_equal(128);
	});

	it("scales a window to a 128-frame block, so window size does not change the reading", _ {
		RenderLoad small;
		small.add(0.5 * 64 / kRate, 64);
		RenderLoad large;
		large.add(0.5 * 128 / kRate, 128);
		expect(small.block_samples()).to_equal(64);
		expect(large.block_samples()).to_equal(64);
	});

	it("reads Long 19's renders (109 frames in 1145 us) under the cull's 80", _ {
		RenderLoad load;
		load.add(1145e-6, 109);
		expect(load.block_samples()).to_equal(59);
	});

	it("ignores a render of no frames", _ {
		RenderLoad load;
		load.add(128 / kRate, 128);
		load.add(1.0, 0);
		expect(load.block_samples()).to_equal(128);
	});

	it("moves halfway to each new render", _ {
		RenderLoad load;
		load.add(128 / kRate, 128);
		load.add(0.0, 128);
		expect(load.block_samples()).to_equal(64);
	});
});
// clang-format on

CPPSPEC_SPEC(render_load)
