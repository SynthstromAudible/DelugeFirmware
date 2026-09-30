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

/// @brief Wait for, or run, roughly one render block.
///
/// For a caller that must block on the render (a higher tier) until it has let go of something; such
/// a caller re-checks its own condition after each call and keeps its own deadline, because what this
/// guarantees differs by platform:
///  - the device (preemptive audio executor): returns once the render has
///    begun another block, or after 20 ms without one;
///  - the host app (the render on its own audio thread): sleeps 1 ms and
///    checks nothing;
///  - the single-executor host harnesses (the render cooperative on the
///    caller's own executor): runs one block's worth of the render's work
///    inline, on the audio tier.
///
/// Never call from the render. [task]
void deluge_audio_await_block(void);

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

/// Convert a capture stamp into how far past the start of the most recently begun render block the event fell.
///
/// A capture stamp is the TX ring's play frame at the moment of the event: the `arrival_stamp` of
/// `deluge_midi_din_read_timed()`, or a trigger-clock handler's argument. Every event lands one block late, by the
/// same amount, so the spacing between events is exact. On the Embassy host every stamp converts to 0 ("now").
/// [task]
uint32_t deluge_audio_stamp_to_render_offset(uint32_t stamp);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_AUDIO_IO_H
