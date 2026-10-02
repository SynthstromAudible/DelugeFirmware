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

/// @brief An idle app loop runs each job only as often as it has work: after boot settles, every job's runs over ten
/// seconds of the app's clock are printed as a rate (IR-JOB), and the wake-driven jobs must not have run at all.
extern "C" {
void deluge_scenario_idle_job_rates_begin(void);
bool deluge_scenario_idle_job_rates_complete(void);
void deluge_scenario_idle_job_rates_dispatch_step(void);
bool deluge_scenario_idle_job_rates_step_done(void);
int32_t deluge_scenario_idle_job_rates_failures(void);
} // extern "C"
