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

/// @brief The C++ oracle for the `deluge_audio_format` corpus differential
/// (crates/deluge_audio_format/tests/corpus_differential.rs), run as `GOLDEN_SCENARIO=audio_header_dump`.
///
/// Reads host filesystem paths from stdin, one per line, until EOF, and prints one line for each:
/// `AUDIO-HEADER ` then `dumpAudioHeaderAsJson()`'s JSON (scenarios/audio_header_dump.h). The prefix lets the
/// test take its lines from stdout among the harness's own. It runs on the booted app, so the parsers see the
/// same global state every other scenario does, and reads the files directly: the card is not involved.
extern "C" {

/// @brief Dump every path on stdin.
void deluge_scenario_audio_header_dump_begin(void);

/// @brief Always true: `begin` does all the work.
bool deluge_scenario_audio_header_dump_complete(void);

/// @brief Nothing to step.
void deluge_scenario_audio_header_dump_dispatch_step(void);

/// @brief Always true.
bool deluge_scenario_audio_header_dump_step_done(void);

/// @brief Always 0: the test compares the output.
int32_t deluge_scenario_audio_header_dump_failures(void);

} // extern "C"
