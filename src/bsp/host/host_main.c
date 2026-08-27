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

/// Host-sim entry point.
///
/// SCAFFOLD STAGE: exercises the BSP boundary to prove the native build + the
/// <libdeluge/...> contract compile and link off-target. As the host build grows
/// to include the portable application (src/deluge), this `main` becomes the host
/// runtime loop: `deluge_app_init(deluge_board())` then drive `deluge_app_render`
/// / `deluge_app_tick` against a host audio sink (the WAV-diff harness).

#include "libdeluge/board.h"
#include "libdeluge/clock.h"
#include "libdeluge/display.h"
#include "libdeluge/memory.h"
#include "libdeluge/system.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h> // getenv, quick_exit
#include <string.h> // strcmp

// The application's monolithic boot + run loop (deluge.cpp). Declared here rather
// than via deluge.h (a C++ header). On hardware src/main.c calls this after the
// arm timer/interrupt bring-up; here deluge_platform_init() is the host analogue.
extern int deluge_main(void);
extern void deluge_host_debug_install(void); // reason-leak signal triggers (host_debug.cpp)

/// The startup banner: the board descriptor, every panel in its display set, and the
/// out-of-range check on the multi-display blit entry point.
///
/// The `display N:` lines are what the `host_displays_*` CTests match on — they are the only
/// automated coverage of host_bsp.c's DELUGE_HOST_DISPLAYS parser (which panels it accepts, and
/// where it stops on a malformed entry) and of deluge_display_blit_oled_n()'s index range check.
/// Keep the format stable, or update sim/CMakeLists.txt's PASS_REGULAR_EXPRESSIONs with it.
static void print_board_info(const DelugeBoard* board) {
	printf("libdeluge host-sim — board: %s (%ux%u pads, %u encoders, %u Hz)\n", board->name, board->pad_grid_width,
	       board->pad_grid_height, board->encoder_count, board->audio_sample_rate_hz);
	printf("display_count: %u\n", board->display_count);
	for (uint8_t i = 0; i < board->display_count; i++) {
		printf("display %u: %ux%u+%u\n", i, board->displays[i].width, board->displays[i].height,
		       board->displays[i].visible_top);
	}
	// A panel index the board does not have must be rejected, not indexed. 9 is past
	// HOST_MAX_DISPLAYS, so this holds whatever DELUGE_HOST_DISPLAYS asked for.
	static const uint8_t probe_pixels[8] = {0};
	printf("blit_oled_n(9) -> %s\n",
	       deluge_display_blit_oled_n(9, probe_pixels, 8, 8) == DELUGE_ERR_PARAM ? "ERR_PARAM" : "UNEXPECTED");
	fflush(stdout);
}

int main(int argc, char** argv) {
	// `--board-info` (equivalently DELUGE_HOST_PRINT_BOARD=1): print the board descriptor and
	// exit 0 without booting the app, so the display-set parsing above can be driven from CTest.
	// deluge_host_boundary would be the natural home for that, but it has not linked since the
	// host BSP grew its host_link/host_debug dependencies (pre-existing on `next`), so the tests
	// run this binary and stop here — before deluge_platform_init() and deluge_main().
	bool board_info_only = false;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--board-info") == 0) {
			board_info_only = true;
		}
	}
	const char* print_board = getenv("DELUGE_HOST_PRINT_BOARD");
	if (print_board && strcmp(print_board, "1") == 0) {
		board_info_only = true;
	}
	if (board_info_only) {
		print_board_info(deluge_board());
		return 0;
	}

	deluge_platform_init();
	deluge_host_debug_install();

	print_board_info(deluge_board());

	deluge_log("host-sim boundary OK — entering deluge_main()\n");

	// Boot and run the app. deluge_main() ends in mainLoop()'s while(1), so this
	// does not return; the process runs until externally terminated (the bounded
	// WAV-diff harness will drive a fixed number of render blocks instead later).
	deluge_main();

	// Unreachable today. Mirror the firmware's no-teardown exit if it ever returns.
	fflush(NULL);
	quick_exit(0);
}
