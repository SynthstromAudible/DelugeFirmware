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
/// @brief The C ABI a stepped scenario exports, declared from its name.
///
/// A stepped scenario `<name>` defines `deluge_scenario_<name>_begin`, `_complete`, `_dispatch_step`, `_step_done`
/// and `_failures` (their contract: scenario_table.h), and scenario_table.cpp names them in the scenario's row. Both
/// declare them with `DELUGE_SCENARIO_DECLARE(<name>);`: the scenario's `.cpp` before its definitions, so the compiler
/// checks each definition against the one declaration, and the table before its rows. Each declaration carries its
/// own `extern "C"`, so the macro works at namespace scope and inside an `extern "C"` block alike.
#define DELUGE_SCENARIO_DECLARE(name)                                                                                  \
	extern "C" void deluge_scenario_##name##_begin(void);                                                              \
	extern "C" bool deluge_scenario_##name##_complete(void);                                                           \
	extern "C" void deluge_scenario_##name##_dispatch_step(void);                                                      \
	extern "C" bool deluge_scenario_##name##_step_done(void);                                                          \
	extern "C" int32_t deluge_scenario_##name##_failures(void)
