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

/// host_pcm — fork an audio player and stream S16 PCM to it. See host_pcm.h.

#include "host_pcm.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// Default player: PipeWire's pw-cat reading raw S16 stereo from stdin. The low --latency
// keeps pad→sound delay tight; the pipe is sized below to bound buffering on our side.
#define HOST_PCM_DEFAULT_FMT "pw-cat --playback --raw --rate %u --channels 2 --format s16 --latency 40ms -"

// Bound our side of the buffering (the rest is the player's own latency). 8 KiB = 2048
// stereo S16 frames ≈ 46 ms at 44.1 kHz.
#define HOST_PCM_PIPE_BYTES 8192

static int pcm_fd = -1; // write end of the pipe into the player's stdin
static pid_t pcm_pid = -1;
static bool pcm_inited;

static bool audio_disabled(const char* cmd) {
	return cmd && (strcmp(cmd, "0") == 0 || strcmp(cmd, "off") == 0 || strcmp(cmd, "none") == 0);
}

bool host_pcm_open(uint32_t sample_rate) {
	if (pcm_inited) {
		return host_pcm_active();
	}
	pcm_inited = true;

	const char* cmd = getenv("DELUGE_HOST_AUDIO");
	if (audio_disabled(cmd)) {
		fprintf(stderr, "host_pcm: audio disabled (DELUGE_HOST_AUDIO=%s)\n", cmd);
		return false;
	}
	char defcmd[256];
	if (!cmd || !cmd[0]) {
		snprintf(defcmd, sizeof(defcmd), HOST_PCM_DEFAULT_FMT, sample_rate);
		cmd = defcmd;
	}

	int pfd[2];
	if (pipe(pfd) < 0) {
		perror("host_pcm: pipe");
		return false;
	}

	pid_t pid = fork();
	if (pid < 0) {
		perror("host_pcm: fork");
		close(pfd[0]);
		close(pfd[1]);
		return false;
	}
	if (pid == 0) {
		// Child: wire the pipe's read end to stdin, then exec the player via the shell so
		// the (overridable) command string can use flags/pipes freely.
		dup2(pfd[0], STDIN_FILENO);
		close(pfd[0]);
		close(pfd[1]);
		execl("/bin/sh", "sh", "-c", cmd, (char*)NULL);
		perror("host_pcm: exec player");
		_exit(127);
	}

	// Parent.
	close(pfd[0]);
	pcm_fd = pfd[1];
	pcm_pid = pid;
	// A player that quits would otherwise SIGPIPE us on the next write; handle EPIPE instead.
	signal(SIGPIPE, SIG_IGN);
#ifdef F_SETPIPE_SZ
	fcntl(pcm_fd, F_SETPIPE_SZ, HOST_PCM_PIPE_BYTES); // best-effort latency bound (Linux)
#endif
	// Non-blocking: the audio task must never wait on the player (a blocking wait would be
	// counted as render time and spike AudioEngine::cpuDireness → voice culling). The caller
	// paces the loop itself; the player's buffer absorbs the jitter.
	int fl = fcntl(pcm_fd, F_GETFL, 0);
	if (fl >= 0) {
		fcntl(pcm_fd, F_SETFL, fl | O_NONBLOCK);
	}
	fprintf(stderr, "host_pcm: audio player started (pid %d): %s\n", (int)pid, cmd);
	return true;
}

bool host_pcm_active(void) {
	return pcm_fd >= 0;
}

void host_pcm_write_s16(const int16_t* interleaved_lr, uint32_t frames) {
	if (pcm_fd < 0) {
		return;
	}
	const uint8_t* p = (const uint8_t*)interleaved_lr;
	size_t n = (size_t)frames * 2u * sizeof(int16_t);
	while (n > 0) {
		ssize_t w = write(pcm_fd, p, n);
		if (w > 0) {
			p += w;
			n -= (size_t)w;
			continue;
		}
		if (w < 0 && errno == EINTR) {
			continue;
		}
		if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			// Player buffer full — we're running slightly ahead. Drop this block's tail rather
			// than block (blocking would inflate the audio-task run time → cpuDireness). A rare,
			// tiny gap at worst; the loop pacing keeps this from happening in steady state.
			return;
		}
		// EPIPE / ECONNRESET / other: the player is gone. Stop audio (caller resumes pacing).
		fprintf(stderr, "host_pcm: player write failed (%s); audio stopped\n", strerror(errno));
		host_pcm_close();
		return;
	}
}

void host_pcm_close(void) {
	if (pcm_fd >= 0) {
		close(pcm_fd); // EOF on the player's stdin → it drains and exits
		pcm_fd = -1;
	}
	if (pcm_pid > 0) {
		waitpid(pcm_pid, NULL, WNOHANG); // best-effort reap; don't block teardown
		pcm_pid = -1;
	}
}
