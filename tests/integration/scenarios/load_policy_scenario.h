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

/// @brief The load policies that need no loader state to pin, run as `GOLDEN_SCENARIO=load_policy`.
///
/// Each prints one `LP-<CHECK> ok=0|1` line, then `LP-RESULT failures=N`:
///
/// - `LP-CANDIDATES`: `AudioFileManager::proposeLoadCandidates` proposes the right paths in the right order. The
///   async loader tries them in exactly this order, so a wrong order opens the wrong file or misses one.
/// - `LP-ADMISSION`: `AudioFileManager::admissibleAsyncLoadPath` agrees with `path_admission_corpus.inc`, which the
///   Rust loader is tested against too. A looser C++ rule asks the loader for a path it refuses, forever.
/// - `LP-ABANDON`: the sample build contract (`storage/audio/sample_build.h`). Abandoning a build publishes nothing,
///   `applyGeometry` refuses an invalid raw data format, and 20,000 begin/abandon rounds release every chunk they
///   adopt, so a browser cursor that never lands on a file cannot exhaust the chunk table. Runs before anything
///   else loads, so its eviction count is its own.
/// - `LP-DEFAULT layer=holder|manager`: `AudioFileHolder::loadFile` and `AudioFileManager::getAudioFileFromFilename`
///   default to the synchronous backing. Against the real loader, which is always active here, a call with the
///   mode omitted loads at once, and the same call made explicitly `AsyncOrBlocking` first reports
///   `LOAD_IN_PROGRESS`; the contrast is what lets a flipped default fail.
extern "C" {

/// @brief Reset the scenario's state and prepare stdout for a piped run.
void deluge_scenario_load_policy_begin(void);

/// @brief Whether the summary has been printed and the run may stop stepping.
bool deluge_scenario_load_policy_complete(void);

/// @brief Queue the next card step.
void deluge_scenario_load_policy_dispatch_step(void);

/// @brief Run the queued card step; whether it has run.
bool deluge_scenario_load_policy_step_done(void);

/// @brief How many checks failed.
int32_t deluge_scenario_load_policy_failures(void);

} // extern "C"
