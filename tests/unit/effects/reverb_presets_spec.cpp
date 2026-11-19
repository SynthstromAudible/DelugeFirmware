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

// effects::kReverbPresets and nearest_reverb_preset: the reverb knob's three rooms and the choice of the nearest.

#include "effects/reverb_presets.h"

#include <cppspec.hpp>

using namespace deluge::effects;

// clang-format off
describe reverb_presets("the reverb presets", ${
	it("hold the small, medium and large rooms", _ {
		expect(kReverbPresets[0].room_size).to_equal(16);
		expect(kReverbPresets[0].damping).to_equal(29);
		expect(kReverbPresets[1].room_size).to_equal(30);
		expect(kReverbPresets[1].damping).to_equal(36);
		expect(kReverbPresets[2].room_size).to_equal(44);
		expect(kReverbPresets[2].damping).to_equal(45);
	});

	it("choose each preset at its own point", _ {
		expect(nearest_reverb_preset(16, 29)).to_equal(size_t{0});
		expect(nearest_reverb_preset(30, 36)).to_equal(size_t{1});
		expect(nearest_reverb_preset(44, 45)).to_equal(size_t{2});
	});

	it("choose the extremes' nearest rooms", _ {
		expect(nearest_reverb_preset(0, 0)).to_equal(size_t{0});
		expect(nearest_reverb_preset(50, 50)).to_equal(size_t{2});
	});

	it("choose, at every menu point, a preset no other is nearer than", _ {
		// The presets lie along a line, so the middle one is nearer than the outer two wherever those are equally
		// far: no menu point is a tie, and the first-on-tie rule never decides.
		const auto distance = [](int32_t room, int32_t damping, size_t p) {
			return detail::menu_distance(room, kReverbPresets[p].room_size)
			       + detail::menu_distance(damping, kReverbPresets[p].damping);
		};
		int wrong = 0;
		for (int32_t room = 0; room <= 50; ++room) {
			for (int32_t damping = 0; damping <= 50; ++damping) {
				const size_t chosen = nearest_reverb_preset(room, damping);
				for (size_t p = 0; p < kReverbPresets.size(); ++p) {
					if (distance(room, damping, p) < distance(room, damping, chosen)) {
						++wrong;
					}
				}
			}
		}
		expect(wrong).to_equal(0);
	});

	it("convert a song's float reverb to menu units by truncation", _ {
		expect(nearest_reverb_preset_of(16.0F / 50.0F, 29.0F / 50.0F)).to_equal(size_t{0});
		expect(nearest_reverb_preset_of(1.0F, 1.0F)).to_equal(size_t{2});
		expect(nearest_reverb_preset_of(0.0F, 0.0F)).to_equal(size_t{0});
	});
});

CPPSPEC_SPEC(reverb_presets)
// clang-format on
