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

/// host_pcm — live audio output for the emulator. Two backends behind one push interface:
///
///   - macOS: a native CoreAudio AudioQueue (the default — no external player, no extra
///     install). Pushed frames land in a lock-free ring the audio device thread drains.
///   - elsewhere, or when DELUGE_HOST_AUDIO names a command: fork that player and write
///     interleaved S16 PCM to its stdin (PipeWire's pw-cat by default on Linux;
///     pacat/aplay/ffplay also work).
///
/// Writing never blocks the audio task — a full buffer/ring drops the block's tail rather
/// than stall (a blocking wait would be counted as render time and spike cpuDireness). The
/// caller paces the cooperative loop itself; the backend's buffer absorbs the jitter.
///
/// DELUGE_HOST_AUDIO overrides the backend: a shell command (reading raw S16 stereo from
/// stdin) forces the subprocess path on any OS; "0"/"off"/"none" disables audio (the caller
/// then falls back to clock pacing).

#ifndef BSP_HOST_PCM_H
#define BSP_HOST_PCM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Start audio output for `sample_rate` Hz, 2ch S16. Returns true if a backend started
/// (audio active); false if disabled (DELUGE_HOST_AUDIO=off) or startup failed — the caller
/// should pace some other way then. Idempotent.
bool host_pcm_open(uint32_t sample_rate);

/// True once a backend is running.
bool host_pcm_active(void);

/// Push `frames` interleaved L/R S16 samples to the active backend. Non-blocking: drops the
/// tail if the backend is full (the caller paces the loop). A dead subprocess deactivates audio.
void host_pcm_write_s16(const int16_t* interleaved_lr, uint32_t frames);

/// Stop the backend and release it (the subprocess drains and exits; the AudioQueue is disposed).
void host_pcm_close(void);

#ifdef __cplusplus
}
#endif

#endif // BSP_HOST_PCM_H
