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

/// host_pcm — live audio output for the emulator.
///
/// Plays the firmware's rendered audio by forking a host audio player (PipeWire's
/// pw-cat by default) and writing interleaved S16 PCM to its stdin. The pipe + the
/// player's device buffer provide *backpressure*: once the player is full, write()
/// blocks, which paces the firmware's cooperative loop to real time — exactly how the
/// SSI/DMA play head paces deluge_audio_drive() on hardware. No audio library is linked
/// (the player is a separate process), so this works in the default -m32 build.
///
/// The player command is overridable via DELUGE_HOST_AUDIO (a shell command reading raw
/// S16 stereo from stdin); set it to "0"/"off"/"none" to disable audio (the caller then
/// falls back to clock pacing). Default targets pw-cat; pacat/aplay/ffplay also work.

#ifndef BSP_HOST_PCM_H
#define BSP_HOST_PCM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Start the audio player for `sample_rate` Hz, 2ch S16. Returns true if a player was
/// launched (audio active); false if disabled (DELUGE_HOST_AUDIO=off) or launch failed —
/// the caller should pace some other way then. Idempotent.
bool host_pcm_open(uint32_t sample_rate);

/// True once a player is running and the pipe is healthy.
bool host_pcm_active(void);

/// Write `frames` interleaved L/R S16 samples, blocking on the player's backpressure
/// (this is the pacing point). A broken pipe (player exited) deactivates audio.
void host_pcm_write_s16(const int16_t* interleaved_lr, uint32_t frames);

/// Close the pipe; the player drains its buffer and exits.
void host_pcm_close(void);

#ifdef __cplusplus
}
#endif

#endif // BSP_HOST_PCM_H
