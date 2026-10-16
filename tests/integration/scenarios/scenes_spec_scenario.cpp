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

#include "scenarios/scenes_spec_scenario.h"

#include "entries.h"

#include "gui/ui/ui.h"
#include "processing/engines/audio_engine.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

namespace {
/// The file's exit code: 0 when every example passed; -1 before it has run.
int g_exit = -1;
} // namespace

extern "C" {

void deluge_scenario_scenes_spec_on_app_init(void) {
	const char* id = std::getenv("SCENES_SPEC");
	if (id == nullptr || id[0] == '\0') {
		std::printf("SCENES-SPEC no file: set SCENES_SPEC\n");
		g_exit = 2;
		return;
	}
	if (std::strcmp(id, "--list") == 0) {
		for (size_t i = 0; i < deluge_scenes_spec_count(); i++) {
			std::printf("SCENES-SPEC-ID %s\n", deluge_scenes_spec_id(i));
		}
		std::fflush(stdout);
		std::_Exit(0);
	}
	const DelugeSceneSpecEntry* entry = deluge_scenes_spec_find(id);
	if (entry == nullptr) {
		std::printf("SCENES-SPEC unknown %s\n", id);
		g_exit = 2;
		return;
	}
	// The specs were written against an app with no UI open: each one installs what it drives. deluge_app_init has
	// already opened the root UI, so it goes first.
	nullifyUIs();
	// Nor did they run under a live audio tier: a render between an example's write and its check would consume state
	// it set (an arpeggiator's restart flag, a voice). Held for the whole file, as `QuietAudio` holds it per example.
	const bool audio_was_locked = AudioEngine::audioRoutineLocked;
	AudioEngine::audioRoutineLocked = true;
	std::printf("SCENES-SPEC %s start\n", id);
	std::fflush(stdout);
	char program[] = "scenes_spec";
	char verbose[] = "--verbose";
	char* argv[] = {program, verbose, nullptr};
	try {
		g_exit = entry->run(2, argv);
	} catch (const std::exception& e) {
		// An exception out of a spec is a failure of that file, whatever it was.
		std::printf("SCENES-SPEC %s threw: %s\n", id, e.what());
		g_exit = 99;
	} catch (...) {
		std::printf("SCENES-SPEC %s threw a non-standard exception\n", id);
		g_exit = 99;
	}
	AudioEngine::audioRoutineLocked = audio_was_locked;
	std::printf("SCENES-SPEC %s exit=%d\n", id, g_exit);
	std::fflush(stdout);
}

void deluge_scenario_scenes_spec_begin(void) {
}

bool deluge_scenario_scenes_spec_complete(void) {
	std::printf("SCENES-SPEC-RESULT failures=%d\n", g_exit == 0 ? 0 : 1);
	std::fflush(stdout);
	return true;
}

void deluge_scenario_scenes_spec_dispatch_step(void) {
}

bool deluge_scenario_scenes_spec_step_done(void) {
	return true;
}

int32_t deluge_scenario_scenes_spec_failures(void) {
	return g_exit == 0 ? 0 : 1;
}

} // extern "C"
