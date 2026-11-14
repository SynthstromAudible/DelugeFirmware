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

#include "param_mapping_reference.h"
#include "plugin-api/types/param_mapping.h"

#include <cppspec.hpp>
#include <cstdint>
#include <limits>

using namespace deluge::plugin;
namespace ref = deluge::plugin::test::reference;

namespace {
/// Words a sweep can't miss: both ends, around zero and the half, and a deterministic spread between.
template <class F>
void each_word(F f) {
	for (int64_t v : {int64_t{INT32_MIN}, int64_t{INT32_MIN} + 1, int64_t{-1}, int64_t{0}, int64_t{1}, int64_t{1} << 30,
	                  (int64_t{1} << 30) - 1, int64_t{INT32_MAX} - (1 << 24), int64_t{INT32_MAX} - (1 << 24) - 1,
	                  int64_t{INT32_MAX} - (1 << 24) + 1, int64_t{INT32_MAX} - (1 << 24) + 2, int64_t{INT32_MAX}}) {
		f(static_cast<int32_t>(v));
	}
	uint32_t x = 0x9E3779B9u;
	for (int i = 0; i < 1'000'000; ++i) {
		x = x * 1664525u + 1013904223u;
		f(static_cast<int32_t>(x));
	}
}
} // namespace

// clang-format off
describe param_mapping("the param mapping kinds", ${
	it("converts words to knob positions as the reference does", _{
		int32_t bad = 0;
		each_word([&](int32_t v) {
			bad += to_knob(MappingKind::STANDARD, v) != ref::standard_to_knob(v);
			bad += to_knob(MappingKind::PAN, v) != ref::standard_to_knob(v);
			bad += to_knob(MappingKind::UNIPOLAR, v) != ref::unipolar_to_knob(v);
			bad += to_knob(MappingKind::CABLE_DEPTH, v) != ref::cable_to_knob(v);
		});
		expect(bad).to_equal(0);
	});
	it("converts every knob position to a word as the reference does", _{
		int32_t bad = 0;
		for (int32_t k = -64; k <= 64; ++k) {
			bad += from_knob(MappingKind::STANDARD, k) != ref::standard_from_knob(k);
			bad += from_knob(MappingKind::PAN, k) != ref::standard_from_knob(k);
			bad += from_knob(MappingKind::UNIPOLAR, k) != ref::unipolar_from_knob(k);
			bad += from_knob(MappingKind::CABLE_DEPTH, k) != ref::cable_from_knob(k);
		}
		expect(bad).to_equal(0);
	});
	it("converts words to menu numbers as the reference does", _{
		int32_t bad = 0;
		each_word([&](int32_t v) {
			bad += to_menu(MappingKind::STANDARD, v) != ref::standard_to_menu(v);
			bad += to_menu(MappingKind::UNIPOLAR, v) != ref::half_to_menu(v);
			bad += to_menu(MappingKind::PAN, v) != ref::pan_to_menu(v);
			bad += to_menu(MappingKind::CABLE_DEPTH, v) != ref::cable_to_menu(v);
		});
		expect(bad).to_equal(0);
	});
	it("converts every menu number to a word as the reference does", _{
		int32_t bad = 0;
		for (int32_t m = 0; m <= 50; ++m) {
			bad += from_menu(MappingKind::STANDARD, m) != ref::standard_from_menu(m);
			bad += from_menu(MappingKind::UNIPOLAR, m) != ref::half_from_menu(m);
		}
		for (int32_t m = -25; m <= 25; ++m) {
			bad += from_menu(MappingKind::PAN, m) != ref::pan_from_menu(m);
		}
		for (int32_t m = menu_min(MappingKind::CABLE_DEPTH); m <= menu_max(MappingKind::CABLE_DEPTH); ++m) {
			bad += from_menu(MappingKind::CABLE_DEPTH, m) != ref::cable_from_menu(m);
		}
		expect(bad).to_equal(0);
	});
	it("reads back every menu number it writes", _{
		int32_t bad = 0;
		for (MappingKind kind : {MappingKind::STANDARD, MappingKind::UNIPOLAR, MappingKind::PAN, MappingKind::CABLE_DEPTH}) {
			for (int32_t m = menu_min(kind); m <= menu_max(kind); ++m) {
				bad += to_menu(kind, from_menu(kind, m)) != m;
			}
		}
		expect(bad).to_equal(0);
		expect(from_menu(MappingKind::STANDARD, 0)).to_equal(INT32_MIN);
		expect(from_menu(MappingKind::STANDARD, 25)).to_equal(-23);
		expect(from_menu(MappingKind::STANDARD, 50)).to_equal(INT32_MAX);
		expect(from_menu(MappingKind::UNIPOLAR, 0)).to_equal(0);
		expect(from_menu(MappingKind::UNIPOLAR, 25)).to_equal(1073741812);
		expect(from_menu(MappingKind::UNIPOLAR, 50)).to_equal(INT32_MAX);
		expect(from_menu(MappingKind::PAN, -25)).to_equal(INT32_MIN);
		expect(from_menu(MappingKind::PAN, 0)).to_equal(0);
		expect(from_menu(MappingKind::PAN, 25)).to_equal(INT32_MAX);
	});
	it("shows a knob position as the popup does", _{
		int32_t bad = 0;
		for (int32_t k = -300; k <= 300; ++k) {
			bad += knob_display_number(ParamFlags{}, k) != ref::knob_pos_for_display(false, k);
			bad += knob_display_number(ParamFlag::CENTRED, k) != ref::knob_pos_for_display(true, k);
		}
		expect(bad).to_equal(0);
	});
	it("turns a preset number into a word without the menu's end snap", _{
		int32_t bad = 0;
		for (int32_t u = -128; u <= 127; ++u) {
			bad += preset_word(static_cast<int8_t>(u)) != ref::preset_word(static_cast<int8_t>(u));
		}
		expect(bad).to_equal(0);
		expect(preset_word(50)).to_equal(2147483602); // not INT32_MAX: Sound's sustain-50 default depends on it
	});
});
// clang-format on

CPPSPEC_SPEC(param_mapping)
