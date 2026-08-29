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

/// Level 3's copy of the button-matrix and encoder-order tables (`src/bsp/host/host_script_tables.c`)
/// cross-checked against the profile it is a copy of (`src/shell/profiles/deluge.cpp`'s
/// `deluge_profile()`). A mismatch here is exactly the drift the split into
/// `host_script_tables.c` (task 5) exists to catch -- see that file's header comment.

#include "bsp/host/host_script_tables.h"
#include "shell/profile.h"
#include "shell/profiles/deluge.h"
#include "ui/core/input.h"

#include "cppspec.hpp"

#include <cstdint>
#include <string>

#include "../support/spec_main.h"

using namespace deluge::ui::shell;

namespace {

/// `EncoderRole` names, in the enum's own declaration order (`ui/core/input.h`) -- `EncoderRole`
/// carries no name table of its own in `src/ui` (nothing on the firmware side ever prints one),
/// so every harness that names a role (this spec, `tests/spec/shell/support/script_runner.h`)
/// keeps its own copy of the same table.
constexpr const char* kEncoderRoleNames[] = {"Select", "Horizontal", "Vertical", "GoldUpper", "GoldLower", "Tempo"};
static_assert(sizeof(kEncoderRoleNames) / sizeof(kEncoderRoleNames[0])
              == static_cast<size_t>(deluge::ui::EncoderRole::Count));

} // namespace

// clang-format off
describe tables("level-3 name tables cross-checked against deluge_profile()", $ {
	it("agrees with the profile's button matrix for every button that has one", _ {
		const BoardProfile& profile = deluge_profile();
		// Every ButtonId with a matrix position, found by reversing the profile's own
		// button_by_matrix -- not every id need have one (a board could omit some), but the
		// Deluge's kMatrix places every id except ButtonId::None somewhere.
		for (uint8_t i = 1; i < static_cast<uint8_t>(ButtonId::Count); i++) {
			const ButtonId id = static_cast<ButtonId>(i);
			bool found = false;
			uint8_t want_x = 0;
			uint8_t want_y = 0;
			for (size_t pos = 0; pos < profile.button_by_matrix.size(); pos++) {
				if (profile.button_by_matrix[pos] == id) {
					found = true;
					want_x = static_cast<uint8_t>(pos % profile.button_cols);
					want_y = static_cast<uint8_t>(pos / profile.button_cols);
					break;
				}
			}
			if (!found) {
				continue; // no matrix position on this board -- nothing to cross-check
			}
			const std::string name = button_name(id);
			uint8_t got_x = 0xFF;
			uint8_t got_y = 0xFF;
			const int ok = ui_script_button_xy(name.c_str(), &got_x, &got_y);
			// Labelled strings, not a bare bool/int compare: `Matchers::Equal` drops any custom
			// failure message (`script_runner.h`'s own note on this), so the entry's own name has
			// to be baked into both sides for a mismatch to say which button it is about.
			expect(name + ": known to ui_script_button_xy")
			    .to_equal(name + ": " + (ok ? "known to ui_script_button_xy" : "UNKNOWN to ui_script_button_xy"));
			if (!ok) {
				continue;
			}
			expect(name + ": (" + std::to_string(got_x) + "," + std::to_string(got_y) + ")")
			    .to_equal(name + ": (" + std::to_string(want_x) + "," + std::to_string(want_y) + ")");
		}
	});

	it("agrees with the profile's encoder order for every EncoderRole", _ {
		const BoardProfile& profile = deluge_profile();
		for (uint8_t i = 0; i < static_cast<uint8_t>(deluge::ui::EncoderRole::Count); i++) {
			const auto role = static_cast<deluge::ui::EncoderRole>(i);
			bool found = false;
			uint8_t want_index = 0;
			for (size_t pos = 0; pos < profile.encoder_by_index.size(); pos++) {
				if (profile.encoder_by_index[pos] == role) {
					found = true;
					want_index = static_cast<uint8_t>(pos);
					break;
				}
			}
			if (!found) {
				continue; // this role is not wired on this board
			}
			const std::string name = kEncoderRoleNames[i];
			const int got = ui_script_encoder_index(name.c_str());
			expect(name + ": index " + std::to_string(got)).to_equal(name + ": index " + std::to_string(want_index));
		}
	});

	it("agrees with the profile in the OTHER direction too, over every matrix coordinate (M-11)", _ {
		// The spec above only reverses the profile's own `button_by_matrix` into the C table, so
		// a `k_buttons` entry the profile does NOT have (a coordinate the profile calls `None`,
		// or one past its matrix bounds) would go unnoticed, and `ui_script_button_name_at` --
		// which names every level-3 `led <Name>` line -- was never called at all. Walking every
		// coordinate the C table's own bounds (0..8, 0..3 -- host_script_tables.c's `kMatrix`) can
		// name, and checking BOTH the "is there a button" question and the name when there is,
		// closes both holes over the same table `ui_script_button_xy`'s check above already read.
		const BoardProfile& profile = deluge_profile();
		for (uint8_t y = 0; y < 4; y++) {
			for (uint8_t x = 0; x < 9; x++) {
				const char* got_name = ui_script_button_name_at(x, y);
				const std::string coord = "(" + std::to_string(x) + "," + std::to_string(y) + ")";

				bool profile_has = false;
				ButtonId want_id = ButtonId::None;
				if (y < profile.button_rows && x < profile.button_cols) {
					const size_t pos = static_cast<size_t>(y) * profile.button_cols + x;
					want_id = profile.button_by_matrix[pos];
					profile_has = want_id != ButtonId::None;
				}

				if (!profile_has) {
					expect(coord + ": " + (got_name == nullptr ? "no button" : std::string("button ") + got_name))
					    .to_equal(coord + ": no button");
					continue;
				}
				const std::string want_name = button_name(want_id);
				expect(coord + ": " + (got_name == nullptr ? "no button" : std::string(got_name)))
				    .to_equal(coord + ": " + want_name);
			}
		}
	});

	it("rejects a button name the profile does not have", _ {
		uint8_t x = 0;
		uint8_t y = 0;
		expect(ui_script_button_xy("Frobnicate", &x, &y)).to_equal(0);
		expect(ui_script_button_xy(nullptr, &x, &y)).to_equal(0);
	});

	it("rejects an encoder role name the profile does not have", _ {
		expect(ui_script_encoder_index("Frobnicate")).to_equal(-1);
		expect(ui_script_encoder_index(nullptr)).to_equal(-1);
	});
});
// clang-format on

DELUGE_SPEC(harness_tables_spec, tables)
