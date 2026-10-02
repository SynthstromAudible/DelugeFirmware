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

/// @brief Cancelling preset loads by ticket. BACK in the preset browser while the preset under the cursor is still
/// staging (LC-STAGE) or loading its samples (LC-SAMPLES) drops that load: the clip keeps its preset, the browser stays
/// where it is, nothing of the dropped load is left behind and no error is shown. A cancel naming a load that has
/// finished, or one a newer request replaced, changes nothing (LC-STALE); a creation refuses a cancel and completes
/// (LC-REFUSED). BACK straight after a fast scroll, the newest scroll-load still queued behind the one it replaces,
/// leaves neither loading (LC-QUEUED).
extern "C" {
void deluge_scenario_load_cancel_begin(void);
bool deluge_scenario_load_cancel_complete(void);
void deluge_scenario_load_cancel_dispatch_step(void);
bool deluge_scenario_load_cancel_step_done(void);
int32_t deluge_scenario_load_cancel_failures(void);
} // extern "C"
