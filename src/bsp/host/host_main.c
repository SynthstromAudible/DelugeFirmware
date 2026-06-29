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
#include "libdeluge/memory.h"
#include "libdeluge/system.h"

#include <stdio.h>
#include <stdlib.h> // quick_exit

// The application's monolithic boot + run loop (deluge.cpp). Declared here rather
// than via deluge.h (a C++ header). On hardware src/main.c calls this after the
// arm timer/interrupt bring-up; here deluge_platform_init() is the host analogue.
extern int deluge_main(void);
extern void deluge_host_debug_install(void); // reason-leak signal triggers (host_debug.cpp)

int main(void) {
	deluge_platform_init();
	deluge_host_debug_install();

	const DelugeBoard* board = deluge_board();
	printf("libdeluge host-sim — board: %s (%ux%u pads, %u encoders, %u Hz)\n", board->name, board->pad_grid_width,
	       board->pad_grid_height, board->encoder_count, board->audio_sample_rate_hz);

	deluge_log("host-sim boundary OK — entering deluge_main()\n");

	// Boot and run the app. deluge_main() ends in mainLoop()'s while(1), so this
	// does not return; the process runs until externally terminated (the bounded
	// WAV-diff harness will drive a fixed number of render blocks instead later).
	deluge_main();

	// Unreachable today. Mirror the firmware's no-teardown exit if it ever returns.
	fflush(NULL);
	quick_exit(0);
}
