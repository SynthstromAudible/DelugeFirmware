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

#include "io/midi/midi_queue_definitions.h"
#include <cstdint>

class ConnectedUSBMIDIDevice;

class MIDIQueueManagerUSBUpstream {
public:
	/// Returns the queued packet count for one upstream USB priority lane.
	static uint16_t queue_count(ConnectedUSBMIDIDevice const* device, QueuePriority priority);
	/// Returns the total number of queued upstream USB packets across all priority lanes.
	static uint32_t total_queued_messages(ConnectedUSBMIDIDevice const* device);
	/// Pops one highest-priority eligible packet, applying CC fairness and CC budget limits.
	static bool pop_priority_message(ConnectedUSBMIDIDevice* device, uint32_t& message_out,
	                                 int32_t& cc_budget_packets_remaining);
	/// Pushes one packed USB MIDI packet into the selected priority lane with CC coalescing/fairness tracking.
	static void push_priority_message(ConnectedUSBMIDIDevice* device, QueuePriority priority, uint32_t message);
	/// Clears all queue storage and fairness bookkeeping for this upstream USB device.
	static void reset_queue_storage(ConnectedUSBMIDIDevice* device);

private:
	/// Replaces newest pending matching CC packet value instead of appending another packet.
	static bool coalesce_queued_cc(ConnectedUSBMIDIDevice* device, uint32_t message);
	/// Pops one queued CC packet chosen by shared round-robin/debt fairness policy.
	static bool pop_fair_queued_cc_message(ConnectedUSBMIDIDevice* device, uint32_t& message_out);
	/// Removes queued CC packet at target offset and compacts remaining packets in-order.
	static bool remove_queued_cc_message_at_offset(ConnectedUSBMIDIDevice* device, uint16_t target_offset,
	                                               uint32_t& message_out);
};
