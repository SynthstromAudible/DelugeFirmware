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

/// @file
/// @brief A level-3 UI script (`UI_SCRIPT`) played into the whole app through the BSP's own input path, one virtual
/// millisecond at a time: every step due by now, then every snapshot due (`UI_SNAPSHOT_DIR`). It completes when the
/// script's `quit` has run and prints `UI-SCRIPT quit at <ms> ms`; a script that fails ends the run with exit 2.

#include "scenarios/support/scenario_declare.h"

#include "harness/level3/host_script.h"

#include <cstdio>

DELUGE_SCENARIO_DECLARE(ui_script);

extern "C" {

void deluge_scenario_ui_script_begin(void) {
	host_script_init();
	if (!host_script_active()) {
		std::fprintf(stderr, "UI-SCRIPT no script: set UI_SCRIPT\n");
		std::fflush(stderr);
		host_script_exit(2);
	}
}

bool deluge_scenario_ui_script_complete(void) {
	if (!host_script_finished()) {
		return false;
	}
	std::printf("UI-SCRIPT quit at %u ms\nUI-SCRIPT-RESULT failures=0\n", static_cast<unsigned>(host_script_now_ms()));
	std::fflush(stdout);
	return true;
}

void deluge_scenario_ui_script_dispatch_step(void) {
	host_script_pump();
}

bool deluge_scenario_ui_script_step_done(void) {
	return true;
}

int32_t deluge_scenario_ui_script_failures(void) {
	return 0;
}

} // extern "C"
