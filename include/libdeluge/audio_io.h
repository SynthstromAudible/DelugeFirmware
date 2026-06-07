/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

/// libdeluge/audio_io.h — duplex block audio.
///
/// The BSP owns the audio buffers and the DMA/SCUX path. It drives the
/// application via `deluge_app_render()` (see app.h) in blocks. This replaces
/// the current arrangement where `audio_engine.cpp` reads the raw SSI/DMA
/// double-buffer by pointer (`getTxBufferStart()`/`getTxBufferCurrentPlace()`).
#ifndef LIBDELUGE_AUDIO_IO_H
#define LIBDELUGE_AUDIO_IO_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Start the audio stream. After this call the BSP will begin invoking
/// `deluge_app_render()`. Idempotent; safe to call once after init.
/// [task]
DelugeStatus deluge_audio_start(void);

/// Stop the audio stream. No further `deluge_app_render()` calls occur after
/// this returns. [task]
void deluge_audio_stop(void);

/// Maximum number of frames the BSP may request in a single `deluge_app_render`
/// call. The application can size scratch buffers from this. [task]
uint32_t deluge_audio_max_block_frames(void);

/// Nominal output sample rate in Hz. [task] [audio]
uint32_t deluge_audio_sample_rate(void);

/// Approximate output latency in frames (render -> DAC), for timing-sensitive
/// features (e.g. aligning MIDI/gate output to audio). [task] [audio]
uint32_t deluge_audio_output_latency_frames(void);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_AUDIO_IO_H
