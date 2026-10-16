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

#include "scenarios/menu_dump_scenario.h"

#include "scenes/menu/menu_dump.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
int32_t g_failures = 0;
} // namespace

extern "C" {

void deluge_scenario_menu_dump_begin(void) {
	std::string json;
	if (!deluge::scenes::menu::dump_menu_tree(json)) {
		g_failures++;
		return;
	}
	const char* path = std::getenv("MENU_DUMP_OUT");
	if (path == nullptr || path[0] == '\0') {
		path = "menu-tree.json";
	}
	std::FILE* f = std::fopen(path, "wb");
	if (f == nullptr || std::fwrite(json.data(), 1, json.size(), f) != json.size() || std::fclose(f) != 0) {
		std::fprintf(stderr, "MENU-DUMP could not write %s\n", path);
		g_failures++;
		return;
	}
	std::printf("MENU-DUMP bytes=%zu\n", json.size());
}

bool deluge_scenario_menu_dump_complete(void) {
	std::printf("MENU-DUMP-RESULT failures=%d\n", static_cast<int>(g_failures));
	std::fflush(stdout);
	return true;
}

void deluge_scenario_menu_dump_dispatch_step(void) {
}

bool deluge_scenario_menu_dump_step_done(void) {
	return true;
}

int32_t deluge_scenario_menu_dump_failures(void) {
	return g_failures;
}

} // extern "C"
