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

#include "io/midi/queue_manager_types/usb_device_cable.h"

#include "io/midi/midi_device_manager.h"
#include "io/midi/midi_queue_manager.h"

namespace {
inline uint8_t status_byte(uint32_t packed) {
	// USB-MIDI event packets store CIN in byte 0 and status in byte 1.
	return static_cast<uint8_t>((packed >> 8) & 0xFF);
}

inline uint8_t data_1(uint32_t packed) {
	// Byte 2 is MIDI data1 for channel/system messages.
	return static_cast<uint8_t>((packed >> 16) & 0xFF);
}

inline uint8_t data_2(uint32_t packed) {
	// Byte 3 is MIDI data2 (for CC this is the value byte).
	return static_cast<uint8_t>((packed >> 24) & 0xFF);
}

inline bool is_channel_cc(uint32_t packed) {
	// Channel-CC status family is 0xBn (high nibble 0x0B).
	return (status_byte(packed) >> 4) == 0x0B;
}
} // namespace

/// Returns queued USB packet count for one lane via monotonic write/read counters.
uint16_t MIDIQueueManagerUSBUpstream::queue_count(ConnectedUSBMIDIDevice const* device, QueuePriority priority) {
	// Monotonic write/read counters: occupancy is their difference for each lane.
	uint8_t p = static_cast<uint8_t>(priority);
	return static_cast<uint16_t>(device->ringBufWriteIdx[p] - device->ringBufReadIdx[p]);
}

/// Returns total queued USB packet count across all priority lanes.
uint32_t MIDIQueueManagerUSBUpstream::total_queued_messages(ConnectedUSBMIDIDevice const* device) {
	// Aggregate backlog across all USB priority lanes.
	uint32_t queued = 0;
	for (uint8_t p = 0; p < QUEUE_PRIORITY_COUNT; p++) {
		queued += queue_count(device, static_cast<QueuePriority>(p));
	}
	return queued;
}

/// Pops one USB packet using strict priority ordering, with fair CC selection.
bool MIDIQueueManagerUSBUpstream::pop_priority_message(ConnectedUSBMIDIDevice* device, uint32_t& message_out,
                                                       int32_t& cc_budget_packets_remaining) {
	for (uint8_t p = QUEUE_PRIORITY_CLOCK; p < QUEUE_PRIORITY_COUNT; p++) {
		QueuePriority priority = static_cast<QueuePriority>(p);
		if (!queue_count(device, priority)) {
			continue;
		}

		if (priority == QUEUE_PRIORITY_CC) {
			// Inspect the CC-lane head packet to decide whether CC fairness rules apply.
			uint16_t head_idx = device->ringBufReadIdx[p] & MIDI_SEND_RING_MASK;
			uint32_t head_message = device->sendDataRingBuf[p][head_idx];

			if (is_channel_cc(head_message)) {
				// Per-transfer CC cap prevents low-priority bursts from dominating the batch.
				if (cc_budget_packets_remaining <= 0) {
					// Skip CC for now and keep scanning lower lanes (e.g. SysEx) this pass.
					continue;
				}
				// Pop one fair-selected CC packet (RR baseline + debt preference).
				if (pop_fair_queued_cc_message(device, message_out)) {
					// Charge one CC slot so the cap is enforced across this transfer assembly.
					cc_budget_packets_remaining--;
					return true;
				}
				// If fair pop failed, do not dequeue arbitrary CC head data in this call.
				continue;
			}
			// Non-CC packets living in the CC lane are handled by the generic dequeue path below.
		}
		// Power-of-two mask wraps index without modulo cost.
		message_out = device->sendDataRingBuf[p][device->ringBufReadIdx[p] & MIDI_SEND_RING_MASK];
		device->ringBufReadIdx[p]++;
		return true;
	}

	return false;
}

/// Pushes one USB packet onto a selected priority lane.
void MIDIQueueManagerUSBUpstream::push_priority_message(ConnectedUSBMIDIDevice* device, QueuePriority priority,
                                                        uint32_t message) {
	// For channel-CC, prefer updating an already-queued matching controller value over appending another packet.
	if (priority == QUEUE_PRIORITY_CC && coalesce_queued_cc(device, message)) {
		return;
	}

	// Power-of-two mask wraps index without modulo cost.
	uint8_t p = static_cast<uint8_t>(priority);
	device->sendDataRingBuf[p][device->ringBufWriteIdx[p] & MIDI_SEND_RING_MASK] = message;
	device->ringBufWriteIdx[p]++;

	if (priority == QUEUE_PRIORITY_CC && is_channel_cc(message)) {
		// Extract controller number from data1 for fairness/debt accounting.
		uint8_t controller = data_1(message);
		if (controller <= kMaxMIDIValue) {
			// Enqueued CC increases this controller's pressure in fair selection.
			MIDIQueueManager::bump_controller_debt(device->usb_cc_fair_controller_debt.data(), controller);
		}
	}
}

/// Coalesces queued USB channel-CC packets by controller/status.
///
/// Searches the USB CC lane for the newest pending packet with the same status
/// byte and controller number, then updates only that packet's value byte.
/// Returns `true` when an in-queue replacement was applied.
bool MIDIQueueManagerUSBUpstream::coalesce_queued_cc(ConnectedUSBMIDIDevice* device, uint32_t message) {
	// Coalescing is defined only for channel-CC packets; other message types must enqueue normally.
	if (!is_channel_cc(message)) {
		return false;
	}

	constexpr uint8_t p = QUEUE_PRIORITY_CC;
	uint16_t queue_size = queue_count(device, QUEUE_PRIORITY_CC);
	if (!queue_size) {
		// No queued CC packets means there is nothing to coalesce in-place.
		return false;
	}

	uint8_t wanted_status = status_byte(message);
	uint8_t wanted_controller = data_1(message);
	int32_t latest_offset = -1;

	// Walk the queued CC lane and remember the newest matching status/controller.
	for (uint16_t offset = 0; offset < queue_size; offset++) {
		// Ring read index + logical offset gives this packet's current queue position.
		uint32_t queued = device->sendDataRingBuf[p][(device->ringBufReadIdx[p] + offset) & MIDI_SEND_RING_MASK];
		if (is_channel_cc(queued) && status_byte(queued) == wanted_status && data_1(queued) == wanted_controller) {
			// Keep updating so the final match is the latest pending packet.
			latest_offset = offset;
		}
	}

	if (latest_offset < 0) {
		// No matching queued status/controller pair was found; caller should enqueue a new packet.
		return false;
	}

	// Replace value byte in-place while preserving queue order for all packets.
	uint16_t target_idx = (device->ringBufReadIdx[p] + latest_offset) & MIDI_SEND_RING_MASK;
	// Keep CIN/status/data1 (low 24 bits) and overwrite only data2 (high byte).
	device->sendDataRingBuf[p][target_idx] =
	    (device->sendDataRingBuf[p][target_idx] & 0x00FFFFFFu) | (static_cast<uint32_t>(data_2(message)) << 24);
	// Treat this coalesced write as fresh controller pressure for fair dequeue.
	MIDIQueueManager::bump_controller_debt(device->usb_cc_fair_controller_debt.data(), wanted_controller);
	return true;
}

/// Removes one queued USB CC packet at a logical offset, atomically.
///
/// Fair dequeue may target a packet that is not at the lane head. This helper
/// copies the selected packet out, rebuilds the remaining CC-lane order, and
/// resets lane cursors to the rebuilt image.
bool MIDIQueueManagerUSBUpstream::remove_queued_cc_message_at_offset(ConnectedUSBMIDIDevice* device,
                                                                     uint16_t target_offset, uint32_t& message_out) {
	constexpr uint8_t p = QUEUE_PRIORITY_CC;
	uint16_t queue_size = queue_count(device, QUEUE_PRIORITY_CC);
	if (target_offset >= queue_size) {
		// Selected logical offset is outside current queue snapshot; cannot remove safely.
		return false;
	}

	// Translate logical queue offset into the wrapped physical ring index.
	uint16_t target_idx = (device->ringBufReadIdx[p] + target_offset) & MIDI_SEND_RING_MASK;
	// Return the selected packet so caller can emit/process it after atomic removal.
	message_out = device->sendDataRingBuf[p][target_idx];

	uint16_t scratch_size = 0;
	// Rebuild a compact queue image by copying every packet except the selected one.
	for (uint16_t i = 0; i < queue_size; i++) {
		if (i == target_offset) {
			// Skip the target packet; it has already been captured in message_out.
			continue;
		}
		// Preserve logical queue order while writing survivors into scratch storage.
		device->usb_cc_reorder_scratch[scratch_size++] =
		    device->sendDataRingBuf[p][(device->ringBufReadIdx[p] + i) & MIDI_SEND_RING_MASK];
	}

	// Rebuild lane content without the selected packet to keep order deterministic.
	device->ringBufReadIdx[p] = 0;
	device->ringBufWriteIdx[p] = 0;
	for (uint16_t i = 0; i < scratch_size; i++) {
		// Replay compacted packets back into the lane in preserved logical order.
		device->sendDataRingBuf[p][device->ringBufWriteIdx[p] & MIDI_SEND_RING_MASK] =
		    device->usb_cc_reorder_scratch[i];
		// Advance write cursor after each restored packet.
		device->ringBufWriteIdx[p]++;
	}

	return true;
}

/// Pops one queued USB channel-CC packet using controller fairness.
///
/// Selection flow:
/// 1. Capture each controller's first queued CC offset.
/// 2. Establish RR baseline from rotating controller cursor.
/// 3. Prefer highest-debt controller when debt is non-zero.
/// 4. Remove selected packet atomically and commit fairness state.
bool MIDIQueueManagerUSBUpstream::pop_fair_queued_cc_message(ConnectedUSBMIDIDevice* device, uint32_t& message_out) {
	constexpr uint8_t p = QUEUE_PRIORITY_CC;
	uint16_t queue_size = queue_count(device, QUEUE_PRIORITY_CC);
	if (!queue_size) {
		// No queued CC packets means there is nothing eligible for fair dequeue.
		return false;
	}

	// Initialize this scan snapshot to "no queued packet found yet" for each controller.
	auto& first_offsets = MIDIQueueManager::initialize_first_controller_offsets(device->usb_cc_fair_first_offsets);
	// Tracks whether this queue snapshot contains any channel-CC packets at all.
	bool saw_any_cc = false;

	// Scan the current CC queue snapshot to collect first-seen offsets per controller.
	for (uint16_t offset = 0; offset < queue_size; offset++) {
		// Map logical scan offset to wrapped ring index, then inspect that queued packet.
		uint32_t queued = device->sendDataRingBuf[p][(device->ringBufReadIdx[p] + offset) & MIDI_SEND_RING_MASK];
		// Fair selection in this pass only considers channel-CC packets.
		if (!is_channel_cc(queued)) {
			continue;
		}
		saw_any_cc = true;
		// For channel-CC packets, data1 is the controller number used as fairness key.
		uint8_t controller = data_1(queued);
		// Record only the first queued packet offset for each controller in this snapshot.
		MIDIQueueManager::record_first_controller_offset(first_offsets, controller, offset);
	}

	if (!saw_any_cc) {
		// Without any channel-CC candidates in this snapshot, fair dequeue cannot select a packet.
		return false;
	}

	// Candidate selection uses shared RR+debt policy logic.
	uint16_t selected_offset = 0;
	uint8_t selected_controller = 0;
	if (!MIDIQueueManager::select_fair_controller_candidate(first_offsets, device->usb_cc_fair_next_controller,
	                                                        device->usb_cc_fair_controller_debt.data(), selected_offset,
	                                                        selected_controller)) {
		// No eligible controller was discovered, so fair dequeue has nothing to emit this pass.
		return false;
	}

	// Do not commit fairness bookkeeping unless the selected packet is removed atomically.
	if (!remove_queued_cc_message_at_offset(device, selected_offset, message_out)) {
		return false;
	}

	// Commit post-dequeue fairness state for the serviced controller, then rotate RR start.
	MIDIQueueManager::commit_fair_controller_service(device->usb_cc_fair_controller_debt,
	                                                 device->usb_cc_fair_next_controller, selected_controller);
	// Selection and removal succeeded; caller can emit the selected packet.
	return true;
}

/// Resets all USB per-priority queues and read/write cursors.
void MIDIQueueManagerUSBUpstream::reset_queue_storage(ConnectedUSBMIDIDevice* device) {
	// storage cleared for deterministic startup, and read/write cursors reset to zero.
	for (auto& queue_lane : device->sendDataRingBuf) {
		queue_lane.fill(0);
	}
	device->ringBufWriteIdx.fill(0);
	device->ringBufReadIdx.fill(0);
}
