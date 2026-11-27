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

#include "abi.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Start the audio stream. After this call the BSP will begin invoking
/// `deluge_app_render()`. Idempotent; safe to call once after init.
/// [task]
DelugeStatus deluge_audio_start(void) DELUGE_NOEXCEPT;

/// Drive the board's audio path: renders the fixed blocks the output queue is missing, calling
/// `deluge_app_render()` once per block with `deluge_audio_block_frames()` frames and the input
/// block aligned to it. On the device the queue is `deluge_audio_queue_blocks()` blocks ahead of
/// the one the codec plays: one render per completed block, up to the whole queue after a late
/// wake. When the codec has reached a block not yet written (an overrun, counted in
/// `deluge_audio_overruns()`), the lost blocks are skipped, output and input alike, and the queue
/// restarts after the block playing. Returns how many times `deluge_app_render()` was invoked
/// (0 = no new audio was needed). The application's audio task calls this; the BSP never invokes
/// the app unprompted. [task] [audio]
uint32_t deluge_audio_drive(void) DELUGE_NOEXCEPT;

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
void deluge_audio_await_block(void) DELUGE_NOEXCEPT;

/// Re-align the input stream's latency window after a glitch or at start-up
/// (the input ring is deeper than the output ring; if an entire output-ring
/// cycle was missed, input latency grows by a lap until corrected). Only
/// requests it: the render, which owns the read cursor, re-aligns at its next
/// block. Cheap; call periodically from a housekeeping task, never from the
/// render. [task]
void deluge_audio_input_resync(void) DELUGE_NOEXCEPT;

/// Convert a capture stamp into how many samples after the next one the render produces the event takes effect at.
///
/// A capture stamp is the codec's play position at the moment of the event, in frames counted from the drive's first
/// block (a wrapping 32-bit count, not a ring position): the `arrival_stamp` of `deluge_midi_din_read_timed()`, or a
/// trigger-clock handler's argument. Every event takes effect the same fixed latency after it happened (the output
/// queue plus two blocks), so the spacing between events is exact; one read so late that its place is already rendered
/// takes effect at once (0), however late. Read `AudioEngine::audioSampleTimer` with it as a pair: the offset counts
/// from the sample the next render starts with. On the Embassy host every stamp converts to 0 ("now"). [task]
uint32_t deluge_audio_stamp_to_render_offset(uint32_t stamp) DELUGE_NOEXCEPT;

/// @brief The frames each `deluge_app_render()` call renders: the board's fixed block. [task] [audio]
uint32_t deluge_audio_block_frames(void) DELUGE_NOEXCEPT;

/// @brief The blocks the board keeps written ahead of the one the codec plays: the output
/// latency, in blocks. [task] [audio]
uint32_t deluge_audio_queue_blocks(void) DELUGE_NOEXCEPT;

/// @brief Blocks the codec reached before they were written (or while they were), since boot:
/// each is an audible glitch while audio plays. An offline render (stem export) drives the queue
/// at its own pace and counts here too, over the dither the codec plays meanwhile. [task] [audio]
uint32_t deluge_audio_overruns(void) DELUGE_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_AUDIO_IO_H
