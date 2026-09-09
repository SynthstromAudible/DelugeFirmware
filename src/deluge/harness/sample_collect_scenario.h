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

/// @file
/// @brief Host-only C ABI for the sample-collect gate: save a song that references audio files with
///        collect-media on, and require both that the samples arrived and that the audio-file list
///        survived the journey.
///
/// ## The check this gate exists for
///
/// `audioFiles` is sorted by `filePath` and searched by bisection. Collecting media REWRITES some of
/// those paths. The old code did the whole collection inside one uninterrupted stretch, so a broken
/// ordering was never observable; the converted code performs the transfers a tick at a time, and
/// something can look in between.
///
/// `SC-ORDERED` is what says the list is still usable afterwards -- every file findable by
/// `searchForExactObject`, and the vector non-decreasing by path. Checking only that the files
/// arrived would miss the entire hazard the design was built to avoid.
///
/// @note `DELUGE_HOST`-only, compiled into `deluge_app`. Inert unless called.
#ifdef DELUGE_HOST

#include <cstdint>

extern "C" {

void deluge_scenario_sample_collect_begin(void);
bool deluge_scenario_sample_collect_complete(void);
void deluge_scenario_sample_collect_dispatch_step(void);
bool deluge_scenario_sample_collect_step_done(void);
int32_t deluge_scenario_sample_collect_failures(void);

} // extern "C"

#endif // DELUGE_HOST
