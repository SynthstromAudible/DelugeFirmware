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

/// host_pcm — stream S16 PCM to the host's audio output. See host_pcm.h.
///
/// Two backends share the push interface below:
///   - macOS: a native CoreAudio AudioQueue (the default; no external player needed).
///   - elsewhere / when DELUGE_HOST_AUDIO names a command: fork that player and pipe S16 to
///     its stdin (PipeWire's pw-cat by default on Linux).

#include "host_pcm.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <stdatomic.h>
#endif

// Default player for the subprocess backend: PipeWire's pw-cat reading raw S16 stereo from
// stdin. The low --latency keeps pad→sound delay tight; the pipe is sized below to bound
// buffering on our side. (macOS uses the built-in CoreAudio backend instead — see below.)
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

#if defined(__APPLE__)
// --- CoreAudio (AudioQueue) backend -----------------------------------------------------
// The audio task pushes frames into a lock-free single-producer/single-consumer ring; the
// AudioQueue's own thread pulls from it to fill each buffer, padding with silence on underrun.
// Pushing never blocks (drops the tail when the ring is full), matching the subprocess path's
// non-blocking contract — a blocking wait would inflate the audio task's measured run time and
// spike AudioEngine::cpuDireness.
#define CA_NUM_BUFFERS 3
#define CA_BUF_FRAMES 512   // frames per AudioQueue buffer
#define CA_RING_FRAMES 8192 // ring capacity in frames (power of two) ≈ 185 ms @ 44.1 kHz

static AudioQueueRef ca_queue;
static AudioQueueBufferRef ca_buffers[CA_NUM_BUFFERS];
static int16_t ca_ring[CA_RING_FRAMES * 2]; // interleaved L,R
static atomic_uint ca_wr;                   // producer cursor (free-running frame count)
static atomic_uint ca_rd;                   // consumer cursor (free-running frame count)
static bool ca_running;

static void ca_callback(void* user, AudioQueueRef q, AudioQueueBufferRef buf) {
	(void)user;
	int16_t* out = (int16_t*)buf->mAudioData;
	unsigned rd = atomic_load_explicit(&ca_rd, memory_order_relaxed);
	unsigned wr = atomic_load_explicit(&ca_wr, memory_order_acquire);
	for (uint32_t i = 0; i < CA_BUF_FRAMES; i++) {
		if (rd != wr) {
			unsigned idx = (rd & (CA_RING_FRAMES - 1)) * 2u;
			out[i * 2] = ca_ring[idx];
			out[i * 2 + 1] = ca_ring[idx + 1];
			rd++;
		}
		else {
			out[i * 2] = 0; // underrun → silence
			out[i * 2 + 1] = 0;
		}
	}
	atomic_store_explicit(&ca_rd, rd, memory_order_release);
	buf->mAudioDataByteSize = CA_BUF_FRAMES * 2u * sizeof(int16_t);
	AudioQueueEnqueueBuffer(q, buf, 0, NULL);
}

static bool coreaudio_open(uint32_t sample_rate) {
	AudioStreamBasicDescription fmt = {0};
	fmt.mSampleRate = (Float64)sample_rate;
	fmt.mFormatID = kAudioFormatLinearPCM;
	fmt.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
	fmt.mBitsPerChannel = 16;
	fmt.mChannelsPerFrame = 2;
	fmt.mFramesPerPacket = 1;
	fmt.mBytesPerFrame = 2u * sizeof(int16_t);
	fmt.mBytesPerPacket = fmt.mBytesPerFrame;

	OSStatus err = AudioQueueNewOutput(&fmt, ca_callback, NULL, NULL, NULL, 0, &ca_queue);
	if (err != noErr) {
		fprintf(stderr, "host_pcm: AudioQueueNewOutput failed (%d)\n", (int)err);
		return false;
	}
	for (int i = 0; i < CA_NUM_BUFFERS; i++) {
		if (AudioQueueAllocateBuffer(ca_queue, CA_BUF_FRAMES * fmt.mBytesPerFrame, &ca_buffers[i]) != noErr) {
			fprintf(stderr, "host_pcm: AudioQueueAllocateBuffer failed\n");
			AudioQueueDispose(ca_queue, true);
			return false;
		}
		memset(ca_buffers[i]->mAudioData, 0, CA_BUF_FRAMES * fmt.mBytesPerFrame); // prime with silence
		ca_buffers[i]->mAudioDataByteSize = CA_BUF_FRAMES * fmt.mBytesPerFrame;
		AudioQueueEnqueueBuffer(ca_queue, ca_buffers[i], 0, NULL);
	}
	err = AudioQueueStart(ca_queue, NULL);
	if (err != noErr) {
		fprintf(stderr, "host_pcm: AudioQueueStart failed (%d)\n", (int)err);
		AudioQueueDispose(ca_queue, true);
		return false;
	}
	ca_running = true;
	fprintf(stderr, "host_pcm: CoreAudio output started @ %u Hz\n", sample_rate);
	return true;
}

static void coreaudio_write(const int16_t* lr, uint32_t frames) {
	unsigned wr = atomic_load_explicit(&ca_wr, memory_order_relaxed);
	unsigned rd = atomic_load_explicit(&ca_rd, memory_order_acquire);
	for (uint32_t i = 0; i < frames; i++) {
		if ((unsigned)(wr - rd) >= CA_RING_FRAMES) {
			break; // ring full — drop the tail rather than block
		}
		unsigned idx = (wr & (CA_RING_FRAMES - 1)) * 2u;
		ca_ring[idx] = lr[i * 2];
		ca_ring[idx + 1] = lr[i * 2 + 1];
		wr++;
	}
	atomic_store_explicit(&ca_wr, wr, memory_order_release);
}

static void coreaudio_close(void) {
	if (ca_running) {
		AudioQueueStop(ca_queue, true);
		AudioQueueDispose(ca_queue, true);
		ca_running = false;
	}
}
#endif // __APPLE__

// --- subprocess backend (fork a player, pipe S16 to its stdin) --------------------------
static bool subprocess_open(const char* cmd) {
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

#if defined(__APPLE__)
	// macOS default: the built-in CoreAudio backend (no external player). An explicit
	// DELUGE_HOST_AUDIO=<command> still routes through the subprocess path below.
	if (!cmd || !cmd[0]) {
		return coreaudio_open(sample_rate);
	}
#endif

	char defcmd[256];
	if (!cmd || !cmd[0]) {
		snprintf(defcmd, sizeof(defcmd), HOST_PCM_DEFAULT_FMT, sample_rate);
		cmd = defcmd;
	}
	return subprocess_open(cmd);
}

bool host_pcm_active(void) {
#if defined(__APPLE__)
	if (ca_running) {
		return true;
	}
#endif
	return pcm_fd >= 0;
}

void host_pcm_write_s16(const int16_t* interleaved_lr, uint32_t frames) {
#if defined(__APPLE__)
	if (ca_running) {
		coreaudio_write(interleaved_lr, frames);
		return;
	}
#endif
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
#if defined(__APPLE__)
	coreaudio_close();
#endif
	if (pcm_fd >= 0) {
		close(pcm_fd); // EOF on the player's stdin → it drains and exits
		pcm_fd = -1;
	}
	if (pcm_pid > 0) {
		waitpid(pcm_pid, NULL, WNOHANG); // best-effort reap; don't block teardown
		pcm_pid = -1;
	}
}
