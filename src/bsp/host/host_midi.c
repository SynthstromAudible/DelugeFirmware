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

/// host_midi — raw DIN MIDI over a host file descriptor. See host_midi.h.

#define _POSIX_C_SOURCE 200809L

#include "host_midi.h"

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int midi_in_fd = -1;  // host → Deluge (we read)
static int midi_out_fd = -1; // Deluge → host (we write)
static bool midi_inited;

// Inbound byte buffer: read in chunks, dole out one byte at a time to the firmware's serial
// MIDI parser (which consumes deluge_midi_din_read_timed() one byte per call).
static uint8_t rx_buf[256];
static uint32_t rx_len;
static uint32_t rx_pos;

static bool midi_disabled(const char* v) {
	return v && (strcmp(v, "0") == 0 || strcmp(v, "off") == 0 || strcmp(v, "none") == 0);
}

// Open `path` non-blocking with the given access mode; -1 on failure.
static int open_nonblock(const char* path, int flags) {
	int fd = open(path, flags | O_NONBLOCK);
	if (fd < 0) {
		fprintf(stderr, "host_midi: could not open %s (%s)\n", path, strerror(errno));
	}
	return fd;
}

// Find the first ALSA snd-virmidi raw device, if the module is loaded.
static const char* autodetect_virmidi(void) {
	static char found[64];
	glob_t g;
	if (glob("/dev/snd/midiC*D*", 0, NULL, &g) == 0 && g.gl_pathc > 0) {
		strncpy(found, g.gl_pathv[0], sizeof(found) - 1);
		globfree(&g);
		return found;
	}
	globfree(&g);
	return NULL;
}

bool host_midi_open(void) {
	if (midi_inited) {
		return host_midi_have_input();
	}
	midi_inited = true;

	const char* in_path = getenv("DELUGE_HOST_MIDI_IN");
	const char* out_path = getenv("DELUGE_HOST_MIDI_OUT");
	const char* both = getenv("DELUGE_HOST_MIDI");

	if (midi_disabled(both)) {
		return false;
	}

	// A single bidirectional device (e.g. a snd-virmidi node), unless explicit IN/OUT override it.
	if ((!in_path || !out_path) && (!both || !both[0])) {
		both = autodetect_virmidi(); // may be NULL if snd-virmidi isn't loaded
	}
	if (both && both[0] && !midi_disabled(both)) {
		int fd = open_nonblock(both, O_RDWR);
		if (fd >= 0) {
			midi_in_fd = midi_out_fd = fd;
		}
	}

	// Explicit one-way endpoints (FIFOs, etc.) override either side.
	if (in_path && in_path[0]) {
		midi_in_fd = open_nonblock(in_path, O_RDONLY);
	}
	if (out_path && out_path[0]) {
		midi_out_fd = open_nonblock(out_path, O_WRONLY);
	}

	signal(SIGPIPE, SIG_IGN);
	if (host_midi_active()) {
		fprintf(stderr, "host_midi: DIN MIDI bridged (in_fd=%d out_fd=%d)\n", midi_in_fd, midi_out_fd);
	}
	return host_midi_have_input();
}

bool host_midi_active(void) {
	return midi_in_fd >= 0 || midi_out_fd >= 0;
}

bool host_midi_have_input(void) {
	return midi_in_fd >= 0;
}

bool host_midi_read_byte(uint8_t* b) {
	if (midi_in_fd < 0) {
		return false;
	}
	if (rx_pos >= rx_len) {
		ssize_t r = read(midi_in_fd, rx_buf, sizeof(rx_buf));
		if (r <= 0) {
			return false; // EAGAIN (nothing now) or EOF/error
		}
		rx_len = (uint32_t)r;
		rx_pos = 0;
	}
	if (b) {
		*b = rx_buf[rx_pos];
	}
	rx_pos++;
	return true;
}

void host_midi_write(const uint8_t* src, uint32_t len) {
	if (midi_out_fd < 0 || len == 0) {
		return;
	}
	const uint8_t* p = src;
	uint32_t n = len;
	while (n > 0) {
		ssize_t w = write(midi_out_fd, p, n);
		if (w > 0) {
			p += w;
			n -= (uint32_t)w;
			continue;
		}
		if (w < 0 && errno == EINTR) {
			continue;
		}
		// EAGAIN (endpoint full) or EPIPE (peer gone): drop the rest rather than block the loop.
		return;
	}
}
