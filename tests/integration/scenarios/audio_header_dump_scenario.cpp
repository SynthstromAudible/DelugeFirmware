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

#include "scenarios/audio_header_dump_scenario.h"

#include "scenarios/audio_header_dump.h"

#include <cstdio>
#include <iostream>
#include <string>

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
