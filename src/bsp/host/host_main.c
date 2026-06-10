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

int main(void) {
	deluge_platform_init();

	const DelugeBoard* board = deluge_board();
	printf("libdeluge host-sim — board: %s (%ux%u pads, %u encoders, %u Hz)\n", board->name, board->pad_grid_width,
	       board->pad_grid_height, board->encoder_count, board->audio_sample_rate_hz);

	DelugeMemoryRegion ext;
	if (deluge_memory_region(0, &ext) == DELUGE_OK) {
		printf("external region: %u MB @ %p\n", ext.size / (1024u * 1024u), ext.base);
	}

	uint64_t t0 = deluge_clock_now();
	deluge_clock_delay_ms(1);
	uint64_t t1 = deluge_clock_now();
	printf("clock: %llu ticks/s, 1ms delay measured %llu us\n", (unsigned long long)deluge_clock_ticks_per_second(),
	       (unsigned long long)(t1 - t0));

	deluge_log("host-sim boundary OK\n");
	return 0;
}
