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

/// deluge_audio_header_dump — headless C++ oracle for the `deluge_audio_format` corpus differential
/// (see `harness/audio_header_dump.h`).
///
/// Reads host filesystem paths, one per line, from stdin until EOF; for each, prints one JSON line to
/// stdout describing `parseSampleHeader`'s and `parseWaveTableHeader`'s (both `insist` settings)
/// outcome. Deliberately does NOT go through `AudioFileManager`/efatfs/`DELUGE_SD_ROOT` the way
/// `deluge_loadcheck` does — the dumper reads raw file bytes directly off the host filesystem and
/// drives the parser through a trivial in-memory `AudioByteSource`, so no SD-card passthrough is
/// needed. Still boots the app the same way `deluge_loadcheck` does (rather than calling the dump
/// function straight from `main()`) so this exercises the parser under the same global-state
/// preconditions every other host binary runs under.
///
/// Usage: deluge_audio_header_dump < paths.txt > dump.jsonl
/// Deterministic. Reads only stdin/argv; touches no SD-card state.

#include "harness/audio_header_dump.h"

#include "OSLikeStuff/scheduler_api.h" // TaskHandle
#include "libdeluge/system.h"          // deluge_platform_init

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

extern "C" int32_t deluge_main(void);
extern TaskHandle startupConditionalTask;

namespace {

void deluge_audio_header_dump_driver() {
	static bool started = false;
	if (started) {
		return;
	}
	started = true;

	std::string path;
	while (std::getline(std::cin, path)) {
		if (path.empty()) {
			continue;
		}
		std::cout << dumpAudioHeaderAsJson(path) << "\n";
	}

	fflush(nullptr);
	quick_exit(0);
}

} // namespace

int main() {
	if (getenv("DELUGE_HOST_DETERMINISTIC") == nullptr) {
		setenv("DELUGE_HOST_DETERMINISTIC", "1", 1);
	}
	if (getenv("DELUGE_HOST_AUDIO") == nullptr) {
		setenv("DELUGE_HOST_AUDIO", "off", 1);
	}
	// deluge.cpp's startup task is gated on isCardReady(), which (host_platform.c) is just "does
	// DELUGE_SD_ROOT name an existing directory" -- this dumper never reads through that path (it
	// reads raw file bytes straight off the host filesystem via std::fopen/fread), but the task still
	// won't fire at all without SOME existing directory here, so point it at the process's own cwd
	// rather than requiring a caller-supplied SD image.
	if (getenv("DELUGE_SD_ROOT") == nullptr) {
		setenv("DELUGE_SD_ROOT", ".", 1);
	}

	startupConditionalTask = deluge_audio_header_dump_driver; // override before deluge_main registers it

	deluge_platform_init();
	deluge_main(); // never returns; deluge_audio_header_dump_driver quick_exit()s

	return 0;
}
