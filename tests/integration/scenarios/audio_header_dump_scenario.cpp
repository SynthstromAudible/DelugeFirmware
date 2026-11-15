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
/// @brief The C++ oracle for the `deluge_audio_format` corpus differential
/// (crates/deluge_rust/tests/audio_format_corpus.rs), run as `GOLDEN_SCENARIO=audio_header_dump`.
///
/// Reads host filesystem paths from stdin, one per line, until EOF, and prints one line for each:
/// `AUDIO-HEADER ` then `dumpAudioHeaderAsJson()`'s JSON (scenarios/audio_header_dump.h). The prefix lets the
/// test take its lines from stdout among the harness's own. It runs on the booted app, so the parsers see the
/// same global state every other scenario does, and reads the files directly: the card is not involved.

#include "scenarios/support/scenario_declare.h"

#include "scenarios/audio_header_dump.h"

#include <cstdio>
#include <iostream>
#include <string>

DELUGE_SCENARIO_DECLARE(audio_header_dump);

void deluge_scenario_audio_header_dump_begin(void) {
	std::string path;
	while (std::getline(std::cin, path)) {
		if (!path.empty()) {
			std::printf("AUDIO-HEADER %s\n", dumpAudioHeaderAsJson(path).c_str());
		}
	}
	std::fflush(stdout);
}

bool deluge_scenario_audio_header_dump_complete(void) {
	return true;
}

void deluge_scenario_audio_header_dump_dispatch_step(void) {
}

bool deluge_scenario_audio_header_dump_step_done(void) {
	return true;
}

int32_t deluge_scenario_audio_header_dump_failures(void) {
	return 0;
}
