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
	static uint16_t queue_count(ConnectedUSBMIDIDevice const* device, QueuePriority priority);
	static uint32_t total_queued_messages(ConnectedUSBMIDIDevice const* device);
	static bool pop_priority_message(ConnectedUSBMIDIDevice* device, uint32_t& message_out,
	                                 int32_t& cc_budget_packets_remaining);
	static void push_priority_message(ConnectedUSBMIDIDevice* device, QueuePriority priority, uint32_t message);
	static void reset_queue_storage(ConnectedUSBMIDIDevice* device);

private:
	static bool coalesce_queued_cc(ConnectedUSBMIDIDevice* device, uint32_t message);
	static bool pop_fair_queued_cc_message(ConnectedUSBMIDIDevice* device, uint32_t& message_out);
	static bool remove_queued_cc_message_at_offset(ConnectedUSBMIDIDevice* device, uint16_t target_offset,
	                                               uint32_t& message_out);
};
