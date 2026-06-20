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

/// host_link — AF_UNIX transport for the host emulator. See host_link.h.

#define _POSIX_C_SOURCE 200809L

#include "host_link.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

// The longest frame we accept inbound is tiny (input events are <= a few bytes);
// outbound display frames are 768+3. The accumulation buffer only ever holds inbound
// traffic, so keep it small but comfortably above the largest legal inbound frame.
#define HOST_LINK_RX_CAP 1024u

static int link_listen_fd = -1; // listening AF_UNIX socket (server)
static int link_conn_fd = -1;   // accepted client (the GUI)
static char link_path[108];     // bound path, so we can unlink() on teardown
static bool link_inited = false;

// Inbound byte accumulator: bytes arrive in arbitrary chunks; we parse whole frames.
static uint8_t rx_buf[HOST_LINK_RX_CAP];
static uint32_t rx_len = 0;

static void link_close(void) {
	if (link_conn_fd >= 0) {
		close(link_conn_fd);
		link_conn_fd = -1;
	}
}

bool host_link_active(void) {
	return link_conn_fd >= 0;
}

bool host_link_init(void) {
	if (link_inited) {
		return host_link_active();
	}
	link_inited = true;

	const char* path = getenv("DELUGE_HOST_LINK");
	if (!path || !path[0]) {
		return false; // headless: no link, offline harness behaviour
	}

	link_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (link_listen_fd < 0) {
		perror("host_link: socket");
		return false;
	}

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
	strncpy(link_path, path, sizeof(link_path) - 1);

	unlink(path); // a stale socket file would make bind() fail with EADDRINUSE
	if (bind(link_listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		perror("host_link: bind");
		close(link_listen_fd);
		link_listen_fd = -1;
		return false;
	}
	if (listen(link_listen_fd, 1) < 0) {
		perror("host_link: listen");
		close(link_listen_fd);
		link_listen_fd = -1;
		return false;
	}

	fprintf(stderr, "host_link: waiting for spark GUI on %s ...\n", path);
	link_conn_fd = accept(link_listen_fd, NULL, NULL); // block until the GUI attaches
	if (link_conn_fd < 0) {
		perror("host_link: accept");
		return false;
	}

	// Reads must not stall the firmware's cooperative loop.
	int flags = fcntl(link_conn_fd, F_GETFL, 0);
	if (flags >= 0) {
		fcntl(link_conn_fd, F_SETFL, flags | O_NONBLOCK);
	}
	// A GUI that quits mid-frame would otherwise SIGPIPE the firmware; ignore it so
	// writes fail with EPIPE and we can downgrade to headless instead of dying.
	signal(SIGPIPE, SIG_IGN);

	fprintf(stderr, "host_link: spark GUI connected\n");
	return true;
}

// Write all `n` bytes, tolerating partial writes; mark the link dead on error.
static void link_write_all(const uint8_t* p, uint32_t n) {
	while (n > 0) {
		ssize_t w = write(link_conn_fd, p, n);
		if (w > 0) {
			p += w;
			n -= (uint32_t)w;
			continue;
		}
		if (w < 0 && (errno == EINTR)) {
			continue;
		}
		if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			continue; // socket buffer full; spin briefly (frames are small)
		}
		// EPIPE / ECONNRESET / other: the GUI is gone. Go headless.
		fprintf(stderr, "host_link: peer write failed (%s); going headless\n", strerror(errno));
		link_close();
		return;
	}
}

void host_link_send(uint8_t type, const uint8_t* data, uint16_t n) {
	if (link_conn_fd < 0) {
		return;
	}
	uint16_t len = (uint16_t)(1u + n); // type byte + data
	uint8_t header[3] = {(uint8_t)(len & 0xFF), (uint8_t)(len >> 8), type};
	link_write_all(header, 3);
	if (n > 0 && link_conn_fd >= 0) {
		link_write_all(data, n);
	}
}

// Pull whatever bytes are available into rx_buf (non-blocking).
static void link_fill(void) {
	if (link_conn_fd < 0) {
		return;
	}
	for (;;) {
		if (rx_len >= HOST_LINK_RX_CAP) {
			return; // full; a frame will be consumed before we read more
		}
		ssize_t r = read(link_conn_fd, rx_buf + rx_len, HOST_LINK_RX_CAP - rx_len);
		if (r > 0) {
			rx_len += (uint32_t)r;
			continue;
		}
		if (r == 0) {
			// Orderly EOF: the GUI closed. Go headless.
			fprintf(stderr, "host_link: peer closed; going headless\n");
			link_close();
			return;
		}
		if (errno == EINTR) {
			continue;
		}
		return; // EAGAIN/EWOULDBLOCK: nothing more right now
	}
}

bool host_link_recv(uint8_t* type, uint8_t* data, uint16_t* inout_len) {
	link_fill();
	if (rx_len < 3) {
		return false; // need length header (2) + at least a type byte (1)
	}
	uint16_t msg_len = (uint16_t)(rx_buf[0] | (rx_buf[1] << 8)); // = 1 + N (type + data)
	if (msg_len == 0) {
		// Misaligned stream; drop one byte and resync on the next call.
		memmove(rx_buf, rx_buf + 1, --rx_len);
		return false;
	}
	uint32_t frame_size = 2u + msg_len;
	if (rx_len < frame_size) {
		return false; // frame not fully arrived yet
	}

	*type = rx_buf[2];
	uint16_t data_len = (uint16_t)(msg_len - 1);
	uint16_t copy = (data_len < *inout_len) ? data_len : *inout_len;
	if (copy > 0) {
		memcpy(data, rx_buf + 3, copy);
	}
	*inout_len = data_len;

	// Consume the frame.
	rx_len -= frame_size;
	memmove(rx_buf, rx_buf + frame_size, rx_len);
	return true;
}
