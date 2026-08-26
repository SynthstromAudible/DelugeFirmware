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

#pragma once

#ifdef __cplusplus
#include <cstdint>
#endif

/// @brief Sizing constants for the per-transfer USB send buffers.
enum QueueSendConstants {
	/// Inner send-buffer size, in 32-bit messages.
	///
	/// @note Increasing this further doesn't work in practice -- appears to be a hardware
	///       limitation (possibly related to USB full-speed mode).
	MIDI_SEND_BUFFER_LEN_INNER = 32,
	/// Inner send-buffer size for a host-mode MIDI connection, in 32-bit messages.
	///
	/// @note Sized to what a Hydrasynth behind a USB hub tolerates; we don't yet have a way to
	///       derive this from the device's own configuration, and haven't seen anything lower
	///       needed so far. Other devices handle more headroom (e.g. a WIDI Bud is fine at 3,
	///       and both are fine at 16 without a hub involved).
	MIDI_SEND_BUFFER_LEN_INNER_HOST = 2,
};

/// @brief Relative send priority for a queued MIDI message.
///
/// Ordering follows the LinnStrument ls_midi.ino strategy: clock > notes > expression > CC >
/// SysEx. SysEx stays lowest priority until it starts draining; once a SysEx message has started
/// sending, its transport units stay contiguous until the terminating USB-MIDI event or DIN 0xF7
/// byte is sent.
typedef enum QueuePriority {
	QUEUE_PRIORITY_CLOCK = 0,
	QUEUE_PRIORITY_NOTES = 1,
	QUEUE_PRIORITY_EXPRESSION = 2,
	QUEUE_PRIORITY_CC = 3,
	QUEUE_PRIORITY_SYSEX = 4,
	QUEUE_PRIORITY_COUNT = 5,
} QueuePriority;

/// @brief Per-lane ring capacities for the USB transport, indexed by QueuePriority.
///
/// @warning Each entry MUST be an exact power of two: MIDIQueueLane masks positions into the ring
///          rather than taking a modulo.
///
/// @note Declared `inline constexpr` rather than plain `constexpr`: MIDIQueueStorage takes these
///       arrays by reference as a non-type template parameter, which requires external linkage. A
///       plain `constexpr` array in a header has internal linkage, so every translation unit would
///       get a distinct entity and the instantiations would not match.
///
/// Sized to what each lane actually holds rather than uniformly. SysEx is largest because a stream is
/// queued all-or-nothing and the biggest the firmware stages is MidiEngine::sysex_fmt_buffer[1024]. CC
/// is next because it holds one entry per distinct pending CC identity. Clock carries single realtime
/// messages and never backs up.
#ifdef __cplusplus
inline constexpr uint16_t k_usb_lane_capacity[QUEUE_PRIORITY_COUNT] = {
    32,   // QUEUE_PRIORITY_CLOCK: single-event realtime messages
    128,  // QUEUE_PRIORITY_NOTES
    128,  // QUEUE_PRIORITY_EXPRESSION: also carries Event CCs and program changes
    256,  // QUEUE_PRIORITY_CC: one entry per distinct Continuous CC identity
    1024, // QUEUE_PRIORITY_SYSEX: 1024 bytes at up to 3 payload bytes per event, with headroom so a
          // full OLED frame still fits behind whatever the backpressure gate let through
};

/// @brief Per-lane ring capacities for the DIN transport, indexed by QueuePriority.
///
/// Same power-of-two requirement and external-linkage rationale as k_usb_lane_capacity; larger here
/// because DIN counts raw bytes per lane rather than packed USB-MIDI events.
inline constexpr uint16_t k_din_lane_capacity[QUEUE_PRIORITY_COUNT] = {
    32,   // QUEUE_PRIORITY_CLOCK: one byte per realtime message
    256,  // QUEUE_PRIORITY_NOTES: 3 bytes per message
    256,  // QUEUE_PRIORITY_EXPRESSION
    512,  // QUEUE_PRIORITY_CC: 3 bytes per message
    2048, // QUEUE_PRIORITY_SYSEX: one complete 1024-byte stream plus headroom
};
#endif
