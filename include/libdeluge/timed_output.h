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

/// libdeluge/timed_output.h — MIDI, CV and gate output delivered at play-timeline stamps.
///
/// A stamp is a frame on the play timeline: the codec's frames counted from the drive's first
/// block, a wrapping 32-bit count compared only by signed difference — the timeline capture stamps
/// are on (see deluge_audio_stamp_to_render_offset). The board sends each event when the codec
/// plays its stamp's frame. A stamp at or behind the play position, or further ahead than the
/// output latency, goes at once; so does DELUGE_STAMP_NOW. Every call sends whatever is already
/// due before it returns.
#ifndef LIBDELUGE_TIMED_OUTPUT_H
#define LIBDELUGE_TIMED_OUTPUT_H

#include "abi.h"
#include "midi_io.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// The stamp for "as soon as possible": before every timed event. Never a real frame's stamp.
#define DELUGE_STAMP_NOW 0u

/// The stamp of the sample `offset_frames` after the first sample of the block being rendered
/// (between renders: of the next block to render). Never DELUGE_STAMP_NOW; DELUGE_STAMP_NOW where
/// there is no play position (the host). [task] [audio]
uint32_t deluge_audio_render_offset_to_stamp(uint32_t offset_frames) DELUGE_NOEXCEPT;

/// The stamp of the frame the codec is playing now: when something sent as soon as possible goes out. Never
/// DELUGE_STAMP_NOW. [task] [audio] [isr]
uint32_t deluge_audio_play_stamp(void) DELUGE_NOEXCEPT;

/// Queue a whole MIDI message on `port` to start at `stamp`: raw bytes on DIN, whole 4-byte
/// USB-MIDI event packets on a USB port. Each port sends in the order queued; a message not yet due
/// holds back those behind it. Returns false, queuing nothing, if the port's queue lacks room for
/// all of it or the port is not connected. [task] [audio]
bool deluge_out_midi_at(DelugeMidiPort port, const uint8_t* bytes, uint32_t len, uint32_t stamp) DELUGE_NOEXCEPT;

/// Bytes `port`'s queue can take now; 0 for a port that is not connected. [task] [audio]
uint32_t deluge_out_midi_space(DelugeMidiPort port) DELUGE_NOEXCEPT;

/// CV `channel`'s DAC word `value` reaches the DAC by `stamp`. Returns false if its queue is full.
/// [task] [audio]
bool deluge_out_cv_at(uint8_t channel, uint16_t value, uint32_t stamp) DELUGE_NOEXCEPT;

/// Gate `channel`'s pin goes to `level` at `stamp`, never before the CV word queued on the same
/// channel for a stamp at or before it has reached the DAC. Returns false if its queue is full.
/// [task] [audio]
bool deluge_out_gate_at(uint8_t channel, bool level, uint32_t stamp) DELUGE_NOEXCEPT;

/// Drop the gate edges queued for the channels in `mask` (bit n: gate n). [task] [audio]
void deluge_out_gate_cancel(uint8_t mask) DELUGE_NOEXCEPT;

/// The timed output service's counts since boot.
typedef struct DelugeOutStats {
	uint32_t late;             ///< events sent more than a sample after their stamp
	uint32_t dropped;          ///< events refused because a queue was full
	uint32_t cv_late_display;  ///< CV words late because a display frame held the bus past their time
	uint32_t display_deferred; ///< display frames that waited for a gap between CV writes
	uint32_t display_forced;   ///< display frames sent without a gap after waiting their longest
} DelugeOutStats;

/// Fill `out` with the service's counts. [task]
void deluge_out_stats(DelugeOutStats* out) DELUGE_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_TIMED_OUTPUT_H
