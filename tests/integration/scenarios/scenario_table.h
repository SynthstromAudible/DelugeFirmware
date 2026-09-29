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

#pragma once

#include <cstdint>

/// @file
/// @brief The stepped scenarios the Embassy host harness can run, found by name.
///
/// A stepped scenario is driven one tick at a time by golden_vt_render's generic driver: `begin` once, then
/// `dispatchStep` and `stepDone` until `complete`, then `failures`. Adding one means writing its five functions and a
/// row in scenario_table.cpp; the driver needs no change.
extern "C" {

struct DelugeScenario {
	/// The name GOLDEN_SCENARIO selects it by.
	const char* name;
	/// Called right after deluge_app_init(), before boot finishes and before the render has run. Null for most.
	void (*onAppInit)(void);
	void (*begin)(void);
	bool (*complete)(void);
	void (*dispatchStep)(void);
	bool (*stepDone)(void);
	int32_t (*failures)(void);
};

/// @brief The stepped scenario named @p name, or null when there is none.
const DelugeScenario* deluge_scenario_find(const char* name);

} // extern "C"
