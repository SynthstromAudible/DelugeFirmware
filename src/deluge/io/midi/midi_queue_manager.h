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

#include "definitions_cxx.hpp"
#include "io/midi/midi_queue_definitions.h"
#include "model/midi/message.h"
#include <array>

class ConnectedUSBMIDIDevice;

/// Shared MIDI queue policy helpers used across transport-specific queue managers.
///
/// This class contains only static shared behavior used by queue manager
/// types in queue_manager_types/:
/// 1. Message classification into queue priorities.
/// 2. Saturating controller-debt accounting.
/// 3. RR+debt fair-controller candidate selection.
///
/// Transport-specific queue storage, enqueue/dequeue, pacing, and mutation
/// logic lives in MIDIQueueManagerDINPorts and MIDIQueueManagerUSBUpstream.
class MIDIQueueManager {
public:
	/// Classifies an outgoing MIDI message into priority groups.
	static QueuePriority classify_message(MIDIMessage message);
	/// Saturating increment helper for per-controller fairness debt.
	static void bump_controller_debt(uint8_t* debt, uint8_t controller);
	/// Shared RR+debt candidate selection used by USB and DIN fair dequeue paths.
	static bool select_fair_controller_candidate(std::array<uint16_t, kMaxMIDIValue + 1> const& first_offsets,
	                                             uint8_t next_controller, uint8_t const* controller_debt,
	                                             uint16_t& selected_offset, uint8_t& selected_controller);
	/// Initializes per-controller first-offset snapshot state to "no offset found" and returns the same array.
	static std::array<uint16_t, kMaxMIDIValue + 1>&
	initialize_first_controller_offsets(std::array<uint16_t, kMaxMIDIValue + 1>& first_offsets);
	/// Records a controller's first queued CC offset once per queue snapshot.
	static void record_first_controller_offset(std::array<uint16_t, kMaxMIDIValue + 1>& first_offsets,
	                                           uint8_t controller, uint16_t offset);
	/// Commits fair-dequeue service for one controller: clear debt and rotate RR cursor.
	static void commit_fair_controller_service(std::array<uint8_t, kMaxMIDIValue + 1>& controller_debt,
	                                           uint8_t& next_controller, uint8_t selected_controller);
	static void commit_fair_controller_service(uint8_t (&controller_debt)[kMaxMIDIValue + 1], uint8_t& next_controller,
	                                           uint8_t selected_controller);
};
