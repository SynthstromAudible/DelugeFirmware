/*
 * Copyright © 2026 Sean Ditny
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

/// @brief Sizing constants for the MIDI send queue's inner buffers and outer ring buffer.
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
	/// Outer send ring-buffer size, in messages.
	///
	/// @note MUST be an exact power of two -- MIDI_SEND_RING_MASK's wraparound masking depends
	///       on it.
	MIDI_SEND_BUFFER_LEN_RING = 1024,
	/// Bitmask for wrapping an index into MIDI_SEND_BUFFER_LEN_RING.
	MIDI_SEND_RING_MASK = MIDI_SEND_BUFFER_LEN_RING - 1,
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
