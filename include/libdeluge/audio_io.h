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

/// Drive the board's audio path. Flushes any rendered-but-unplayed samples to
/// the DAC ring; then, while the previous block is fully flushed and the
/// board's pacing policy asks for more (on RZ/A1L: render as far as the DMA
/// play head has advanced, doubled under light load, capped at the ring size,
/// rounded to a multiple of 4), calls `deluge_app_render()` with the chosen
/// frame count and an input block aligned to it. Returns how many times
/// `deluge_app_render()` was invoked (0 = no new audio was needed). The
/// application's audio task calls this; the BSP never invokes the app
/// unprompted. [task]
uint32_t deluge_audio_drive(void);

/// Re-align the input stream's latency window after a glitch or at start-up
/// (the input ring is deeper than the output ring; if an entire output-ring
/// cycle was missed, input latency grows by a lap until corrected). Cheap;
/// call periodically from a housekeeping task, never from the render. [task]
void deluge_audio_input_resync(void);

/// During/after a `deluge_app_render()` call: how many frames from now until
/// the DAC plays sample `offset_frames` of the current render block, modulo
/// the output ring (0 means a full ring from now). Optionally reports how many
/// frames of the block's play position have elapsed since the block's render
/// began (`*elapsed_frames_out`, unmasked). Used to arm
/// `deluge_midi_gate_timer_arm()` sample-accurately. Single play-head poll.
/// [audio]
uint32_t deluge_audio_frames_until_block_offset(uint32_t offset_frames, uint32_t* elapsed_frames_out);

/// Convert a board capture timestamp (the `arrival_ticks` of
/// `deluge_midi_din_read_timed()`, or a trigger-clock edge stamp) into a frame
/// offset relative to the application's render-timeline head, modulo the
/// output ring. The app adds this to its sample timer to place external clock
/// edges on its audio timeline. [task]
uint32_t deluge_audio_stamp_to_render_offset(uint32_t stamp);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_AUDIO_IO_H
