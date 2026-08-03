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

// MUST be an exact power of two
enum MidiQueueRingConstants {
	MIDI_SEND_BUFFER_LEN_RING = 1024,
	MIDI_SEND_RING_MASK = MIDI_SEND_BUFFER_LEN_RING - 1,
};

// Priority order is aligned with the LinnStrument ls_midi.ino strategy:
// clock > notes > expression > CC > SysEx.
typedef enum QueuePriority {
	QUEUE_PRIORITY_CLOCK = 0,
	QUEUE_PRIORITY_NOTES = 1,
	QUEUE_PRIORITY_EXPRESSION = 2,
	QUEUE_PRIORITY_CC = 3,
	QUEUE_PRIORITY_SYSEX = 4,
	QUEUE_PRIORITY_COUNT = 5,
} QueuePriority;
