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

#include "io/midi/midi_queue_manager.h"
#include "io/midi/midi_engine.h"
#include "timers_interrupts/timers_interrupts.h"

extern "C" {
#include "RZA1/uart/sio_char.h"
#include "drivers/uart/uart.h"

extern uint8_t anyUSBSendingStillHappening[];
}

#include <algorithm>
#include <cstring>

/*
 * MIDI Queue Manager Information
 *
 * This file contains shared queue policy plus USB and DIN transport queue managers.
 * The queue managers are used by ConnectedUSBMIDIDevice and ConnectedDINMIDIDevice to stage outgoing messages for USB
 * or DIN transfer.
 */

namespace {
inline uint8_t status_byte(uint32_t packed) {
	// In a packed USB-MIDI event, byte 1 is the MIDI status byte.
	return static_cast<uint8_t>((packed >> 8) & 0xFF);
}

inline uint8_t data_1(uint32_t packed) {
	// In a packed USB-MIDI event, byte 2 is MIDI data1.
	return static_cast<uint8_t>((packed >> 16) & 0xFF);
}

inline uint8_t data_2(uint32_t packed) {
	// In a packed USB-MIDI event, byte 3 is MIDI data2.
	return static_cast<uint8_t>((packed >> 24) & 0xFF);
}

inline uint8_t usb_cin(uint32_t packed) {
	// In a packed USB-MIDI event, the low nibble of byte 0 is the Code Index Number.
	return static_cast<uint8_t>(packed & 0x0F);
}

inline uint32_t replace_data_2(uint32_t packed, uint8_t value) {
	// Packed USB-MIDI event byte layout:
	// byte 0 = cable/CIN
	// byte 1 = MIDI status
	// byte 2 = data1/CC number
	// byte 3 = data2/value
	//
	// Preserve bytes 0-2 and replace only byte 3.
	return (packed & 0x00FFFFFFu) | (static_cast<uint32_t>(value) << 24);
}

inline bool is_packed_channel_cc(uint32_t packed) {
	// USB CC detection is based on the MIDI status byte inside the packed event.
	return MIDIQueueManager::is_channel_cc_status_byte(status_byte(packed));
}

inline bool is_usb_sysex_event(uint32_t packed) {
	// USB-MIDI SysEx events use CIN 0x4 for start/continue and 0x5..0x7 for endings.
	uint8_t cin = usb_cin(packed);
	return cin >= 0x4 && cin <= 0x7;
}

inline bool is_usb_sysex_end_event(uint32_t packed) {
	// CIN 0x5, 0x6, and 0x7 terminate a USB-MIDI SysEx stream with 1, 2, or 3 bytes.
	uint8_t cin = usb_cin(packed);
	return cin >= 0x5 && cin <= 0x7;
}

// Shared constants for MIDI message lengths and SysEx start/end bytes.
// The highest-priority DIN lane is drained one realtime/system byte at a time.
constexpr int32_t k_midi_realtime_message_bytes = 1;
// Channel voice messages are at most three serial bytes: status, data1, data2.
constexpr int32_t k_midi_channel_message_max_bytes = 3;
// DIN SysEx is queued as raw bytes and drains one serial byte at a time.
constexpr int32_t k_midi_sysex_byte_count = 1;
constexpr uint8_t k_midi_sysex_start_byte = 0xF0;
constexpr uint8_t k_midi_sysex_end_byte = 0xF7;

// USB queue constants.
// USB-MIDI events are packed into a uint32_t with byte 0 in the lowest 8 bits:
// byte 0 = cable/CIN, byte 1 = MIDI status, byte 2 = data1, byte 3 = data2.
constexpr uint8_t k_usb_midi_event_packet_bytes = 4;
// A USB-MIDI event can carry up to three MIDI payload bytes after the CIN/cable byte.
constexpr uint8_t k_usb_midi_event_payload_bytes = 3;
// When a USB device has more than this many queued messages, opportunistically
// trigger a flush to keep latency bounded during bursts.
constexpr uint32_t k_usb_flush_backlog_message_threshold = 16;
// Maximum number of lowest-priority CC messages staged in one USB transfer. This
// lets later clock/note/expression traffic preempt dense automation sooner.
constexpr int32_t k_usb_cc_message_allowance_per_transfer = 8;

// DIN serial pacing constants.
// DIN pacing uses Q8 fixed-point bytes so fractional send allowance can accrue
// smoothly between audio callbacks.
constexpr int32_t k_serial_allowance_fraction_bits = 8;
constexpr int32_t k_serial_allowance_fraction_scale = 1 << k_serial_allowance_fraction_bits;
// DIN link throughput in Q8 fixed-point bytes/second (31.25 kbps ~= 3125 bytes/s).
constexpr int32_t k_serial_bytes_per_second_Q8 = 3125 * k_serial_allowance_fraction_scale;
// Maximum accumulated send allowance (Q8 bytes) allowed for one burst after idle time.
constexpr int32_t k_serial_send_allowance_max_Q8 = MIDI_TX_BUFFER_SIZE * k_serial_allowance_fraction_scale;
// Reserve some UART TX space so we do not fill the hardware buffer to the edge.
constexpr int32_t k_serial_uart_headroom_bytes = 16;
// Limit how much lowest-priority CC traffic can be staged ahead in the DIN UART buffer.
// This keeps dense CC bursts from sitting on the wire ahead of later clock or note bytes
// even when the software queues themselves are draining cleanly.
constexpr int32_t k_serial_buffered_cc_bytes_cap = 24;

} // namespace

/// Classifies an outgoing MIDI message into shared queue priorities.
QueuePriority MIDIQueueManager::classify_message(MIDIMessage message) {
	if (message.isSystemMessage()) {
		// Keep system/realtime messages in the highest-priority lane.
		return QUEUE_PRIORITY_CLOCK;
	}

	switch (static_cast<MIDIStatusType>(message.statusType)) {
	case MIDIStatusType::NoteOff:
	case MIDIStatusType::NoteOn:
		// Note on/off events are timing-sensitive, but below clock/system messages.
		return QUEUE_PRIORITY_NOTES;

	case MIDIStatusType::PolyphonicAftertouch:
	case MIDIStatusType::ChannelAftertouch:
	case MIDIStatusType::PitchBend:
		// Expression data is important for feel, but can sit behind notes.
		return QUEUE_PRIORITY_EXPRESSION;

	case MIDIStatusType::ControlChange:
		if (message.data1 == CC_EXTERNAL_MOD_WHEEL || message.data1 == CC_EXTERNAL_MPE_Y) {
			// Mod wheel and MPE Y-axis are expressive CCs that should be prioritized above other CCs.
			return QUEUE_PRIORITY_EXPRESSION;
		}
		// Other CC traffic is the lowest-priority channel voice traffic.
		return QUEUE_PRIORITY_CC;

	default:
		// Unknown/non-note channel messages are safest in the lowest channel lane.
		return QUEUE_PRIORITY_CC;
	}
}

/* MIDI Queue Manager USB Transport
 *
 * This class manages a per-device USB-MIDI queue with multiple priority lanes.
 * It is used by ConnectedUSBMIDIDevice to stage outgoing messages for USB transfer.
 *
 * When a USB send transaction starts, the ConnectedUSBMIDIDevice calls into this queue manager
 * to consume queued messages and fill the transfer buffer. The transfer buffer is then sent
 * to the device via the USB driver.
 *
 * After a USB send transaction completes, the ConnectedUSBMIDIDevice may call into this
 * queue manager again to consume more queued messages if any remain.
 *
 * Priority lanes are drained in strict order: clock > notes > expression > CC > SysEx.
 *
 * CC messages may be coalesced into an existing queued entry instead of appended,
 * to avoid sending stale values when a later message has already superseded it.
 *
 * The queue manager also tracks whether a SysEx stream is active, and keeps draining
 * only SysEx events until the terminating event is sent. This prevents interleaving
 * other MIDI traffic into a SysEx stream that may be split across multiple transfers.
 *
 * The queue manager does not handle USB transfer logic or callbacks; it only manages
 * the queued messages and their priorities. The ConnectedUSBMIDIDevice class handles
 * the actual USB send transactions and calls into this queue manager to get the next
 * messages to send.
 *
 * Message queueing flow (Normal messages):
 * Source
 *  -> MIDIMessage
 *  -> MIDICableUSB::sendMessage or MidiEngine::sendUsbMidi
 *  -> setupUSBMessage
 *  -> add virtual cable number
 *  -> ConnectedUSBMIDIDevice::enqueue_message
 *  -> MIDIQueueManagerUSB::enqueue_message
 *  -> classify_packed_usb_priority
 *  -> message is queued into correct priority lane
 *
 * Message queuing flow (SysEx messages):
 * Source
 *  -> SysEx bytes
 *  -> MIDICableUSB::sendSysex
 *  -> USB-MIDI event chunks
 *  -> ConnectedUSBMIDIDevice::enqueue_message
 *  -> MIDIQueueManagerUSB::enqueue_message
 *  -> message is queued into QUEUE_PRIORITY_SYSEX priority lane
 *
 * Message sending flow:
 * MidiEngine::flushMIDI
 *   -> MidiEngine::flushUSBMIDIOutput
 *   -> ConnectedUSBMIDIDevice::consume_queued_messages
 *   -> MIDIQueueManagerUSB::consume_queued_messages
 *   -> message is drained into dataSendingNow buffer in priority order
 *   -> usb_send_start_rohan sends the dataSendingNow buffer to the connected USB device

 * usbSendCompleteAsHost / usbSendCompleteAsPeripheral
 *   -> ConnectedUSBMIDIDevice::consume_queued_messages
 *   -> MIDIQueueManagerUSB::consume_queued_messages
 *   -> any remaining queued messages are drained into dataSendingNow buffer in priority order
 *   -> usb_send_start_rohan sends the dataSendingNow buffer to the connected USB device
 */

/// Resets all USB per-priority queues and CC scheduling bookkeeping.
void MIDIQueueManagerUSB::reset_queue_storage() {
	queue_manager_.clear_all();
	sysex_drain_active_ = false;
}

bool MIDIQueueManagerUSB::has_buffered_send_data() const {
	// True when at least one queued USB message exists across any priority lane.
	return queue_manager_.total_queued_messages() > 0;
}

int MIDIQueueManagerUSB::send_buffer_space() const {
	// Total queued USB messages currently buffered across all priority lanes.
	uint32_t queued = queue_manager_.total_queued_messages();
	// Maximum messages we can queue: usable slots per lane (ring-1) times number of lanes.
	uint32_t total_capacity_messages = (MIDI_SEND_BUFFER_LEN_RING - 1) * QUEUE_PRIORITY_COUNT;
	// Can't queue anymore: return 0 bytes of remaining capacity.
	if (queued >= total_capacity_messages) {
		return 0;
	}

	// Each 4-byte USB-MIDI event contains up to 3 bytes of MIDI payload.
	// Report remaining capacity by payload bytes, not by 4-byte USB event slots.
	return (total_capacity_messages - queued) * k_usb_midi_event_payload_bytes;
}

QueuePriority MIDIQueueManagerUSB::classify_packed_usb_priority(uint32_t packed) {
	if (is_usb_sysex_event(packed)) {
		// SysEx USB-MIDI events use CIN 0x4..0x7 and are already chunked by the caller.
		return QUEUE_PRIORITY_SYSEX;
	}

	// Non-SysEx events are classified by decoding the MIDI status/data bytes.
	uint8_t status = status_byte(packed);
	MIDIMessage decoded{
	    .statusType = static_cast<uint8_t>((status >> 4) & 0x0F),
	    .channel = static_cast<uint8_t>(status & 0x0F),
	    .data1 = data_1(packed),
	    .data2 = data_2(packed),
	};
	return MIDIQueueManager::classify_message(decoded);
}

void MIDIQueueManagerUSB::enqueue_message(uint32_t full_message) {
	// Total messages currently queued across all priority lanes for this device.
	uint32_t queued = queue_manager_.total_queued_messages();
	// If backlog grows, opportunistically kick a flush to keep latency bounded.
	if (queued > k_usb_flush_backlog_message_threshold) {
		// Only trigger a new flush when a send transaction is not already active.
		if (anyUSBSendingStillHappening[0] == 0) {
			midiEngine.flushUSBMIDIOutput();
		}
	}

	// Determine which priority lane this packed USB-MIDI event belongs to.
	QueuePriority priority = classify_packed_usb_priority(full_message);
	// Occupancy of just the selected priority lane we are about to enqueue into.
	uint16_t queue_size = queue_manager_.queue_count(static_cast<uint8_t>(priority));
	// Keep one slot free in each ring so full/empty states stay distinguishable.
	if (queue_size >= (MIDI_SEND_BUFFER_LEN_RING - 1)) {
		// If nothing is currently transmitting, try flushing now to free space.
		if (anyUSBSendingStillHappening[0] == 0) {
			midiEngine.flushUSBMIDIOutput();
		}
		// Re-check after opportunistic flush.
		queue_size = queue_manager_.queue_count(static_cast<uint8_t>(priority));
		if (queue_size >= (MIDI_SEND_BUFFER_LEN_RING - 1)) {
			// Still full: drop this message rather than overwrite unread queued data.
			// TODO: show some error message
			return;
		}
	}

	// CC messages may be coalesced into an existing queued entry instead of appended.
	bool queued_ok = enqueue_message_with_cc_policy(priority, full_message);

	// Signal that at least one USB message is waiting so flush logic can schedule transmission.
	if (queued_ok) {
		anythingInUSBOutputBuffer = true;
	}
}

/// Drains queued USB messages into the smaller `dataSendingNow` transfer buffer.
bool MIDIQueueManagerUSB::consume_queued_messages(uint8_t* data_sending_now, uint8_t& num_bytes_sending_now,
                                                  bool usb_host_mode) {
	// Snapshot total queued messages across all priority lanes.
	uint32_t queued = queue_manager_.total_queued_messages();
	if (queued == 0) {
		// Nothing pending: caller should not start a USB send transfer.
		return false;
	}

	int32_t i = 0;
	// bfredl:
	// many devices do not accept more than 64 bytes of data at a time
	// likely this can be inferred from the device metadata somehow?

	// Mark Adams:
	// some seem to take even less, especially with hubs involved. The hydrasynth seems to only respond to a max of
	// 2 messages per transfer, the third gets blocked. For MPE this leads to ignoring note ons as the x and y
	// resets are sent before the note on
	uint32_t max_size = usb_host_mode ? MIDI_SEND_BUFFER_LEN_INNER_HOST : MIDI_SEND_BUFFER_LEN_INNER;
	// Build at most one USB transfer worth of messages: no more than queued, and no more than the mode/device cap.
	int32_t to_send = std::min<uint32_t>(queued, max_size);
	// Keep CC bursts bounded per transfer so fresh clock/notes can preempt sooner.
	int32_t cc_allowance_messages_remaining = k_usb_cc_message_allowance_per_transfer;
	// Serialize `to_send` prioritized 4-byte USB messages into dataSendingNow.
	for (i = 0; i < to_send; i++) {
		uint32_t message = 0;
		// Pull one prioritized USB message from the multi-lane ring queues.
		USBSendContext context{message, cc_allowance_messages_remaining};
		bool popped = false;

		// Once a USB SysEx stream has started, keep draining only SysEx events
		// until the terminating CIN is sent. USB transfer-size limits may split
		// the stream across transfers, but other MIDI must not be interleaved.
		if (sysex_drain_active_) {
			popped = pop_sysex_message(context);
			if (!popped) {
				// Defensive recovery for malformed/truncated queue state: avoid
				// permanently blocking other lanes if the SysEx lane ran dry.
				sysex_drain_active_ = false;
			}
		}

		if (popped) {
			// Active SysEx already supplied this transfer slot, so copy it out
			// and skip normal priority scanning to avoid interleaving other MIDI.
			memcpy(data_sending_now + (i * k_usb_midi_event_packet_bytes), &message, k_usb_midi_event_packet_bytes);
			continue;
		}

		// Scan lanes from highest to lowest priority and take one message from the first eligible lane.
		for (uint8_t lane = static_cast<uint8_t>(QUEUE_PRIORITY_CLOCK);
		     lane < static_cast<uint8_t>(QUEUE_PRIORITY_COUNT); lane++) {
			QueuePriority priority = static_cast<QueuePriority>(lane);
			if (!queue_manager_.queue_count(static_cast<uint8_t>(priority))) {
				// Empty lanes cannot contribute to this transfer slot.
				continue;
			}

			if (priority == QUEUE_PRIORITY_CC) {
				auto cc_result = handle_cc_lane(priority, context);
				if (cc_result == MIDIQueueManager::PriorityLaneTraversalResult::Popped) {
					// The CC scheduler selected and removed a CC from somewhere in the lane.
					popped = true;
					break;
				}
				if (cc_result == MIDIQueueManager::PriorityLaneTraversalResult::Abort) {
					// The CC lane cannot safely provide a message right now.
					break;
				}
				if (cc_result == MIDIQueueManager::PriorityLaneTraversalResult::SkipLane) {
					// Leave CC untouched for this transfer slot and continue traversal.
					continue;
				}
				if (cc_result != MIDIQueueManager::PriorityLaneTraversalResult::PopLane) {
					// Unknown traversal result: do not pop this lane.
					continue;
				}
			}

			bool lane_popped =
			    priority == QUEUE_PRIORITY_SYSEX ? pop_sysex_message(context) : pop_lane(priority, context);
			if (lane_popped) {
				// Lane pop succeeded; this transfer slot is filled.
				popped = true;
				break;
			}
		}
		if (!popped) {
			// Queue state changed unexpectedly (or became empty); stop assembling this transfer.
			break;
		}
		// Pack each 32-bit USB-MIDI event contiguously for the driver DMA/transfer buffer.
		memcpy(data_sending_now + (i * k_usb_midi_event_packet_bytes), &message, k_usb_midi_event_packet_bytes);
	}

	// `i` is the number of USB messages assembled, each encoded USB-MIDI event is 4 bytes.
	num_bytes_sending_now = static_cast<uint8_t>(i * k_usb_midi_event_packet_bytes);

	// Tell caller whether we assembled at least one message to transmit.
	return i > 0;
}

bool MIDIQueueManagerUSB::pop_lane(QueuePriority priority, USBSendContext& context) {
	// USB queue entries are already complete USB-MIDI events, so one pop is one message.
	return queue_manager_.pop_head(static_cast<uint8_t>(priority), context.message_out);
}

bool MIDIQueueManagerUSB::pop_sysex_message(USBSendContext& context) {
	if (!pop_lane(QUEUE_PRIORITY_SYSEX, context)) {
		return false;
	}

	// The SysEx lane only contains USB-MIDI SysEx events. CIN 0x4 means the
	// logical SysEx continues; CIN 0x5..0x7 means this event terminates it.
	sysex_drain_active_ = !is_usb_sysex_end_event(context.message_out);
	return true;
}

/* **************************************************************** */
/* USB MIDI CC specific queuing and scheduling functions start here */
/* **************************************************************** */

bool MIDIQueueManagerUSB::enqueue_message_with_cc_policy(QueuePriority priority, uint32_t queued_message) {
	// For CC messages, first try to update an already-queued message for the same
	// status/CC number. If this succeeds, no new queue entry is needed.
	if (priority == QUEUE_PRIORITY_CC && coalesce_cc_message(queued_message)) {
		return true;
	}

	// Otherwise append the message to its priority lane as normal.
	bool queued_ok = enqueue_priority_message(priority, queued_message);
	if (queued_ok && priority == QUEUE_PRIORITY_CC && is_packed_channel_cc(queued_message)) {
		// Record that this CC number has unsent work so scheduled dequeue can prefer it.
		queue_manager_.bump_cc_debt(data_1(queued_message));
	}
	return queued_ok;
}

bool MIDIQueueManagerUSB::coalesce_cc_message(uint32_t queued_message) {
	if (!is_packed_channel_cc(queued_message)) {
		// Only channel CC messages participate in value coalescing.
		return false;
	}

	// Coalescing only updates the value byte. Cable/CIN, status, channel, and
	// CC number stay in the original queue position.
	uint8_t wanted_status = status_byte(queued_message);
	uint8_t wanted_cc_number = data_1(queued_message);
	auto begin_scan = [this](uint16_t& scan_position, uint16_t& limit) {
		// Initialize a USB CC-lane scan and report how many queued events it can inspect.
		return begin_cc_message_scan(scan_position, limit);
	};
	auto next_scan = [this](uint16_t& scan_position, uint16_t limit, uint16_t& candidate_offset, uint8_t& status,
	                        uint8_t& cc_number) {
		// Read the next USB event and adapt it into the generic coalescing result shape.
		MIDIQueueManager::CCMessageScanEntry message{};
		return MIDIQueueManager::adapt_cc_coalesce_scan_result(next_cc_message(scan_position, limit, message), message,
		                                                       candidate_offset, status, cc_number);
	};
	auto update_matched = [this, queued_message, wanted_status, wanted_cc_number](uint16_t latest_offset) {
		// The scan above runs unguarded, so between finding this offset and writing it the consumer may have
		// removed an entry, which advances read_pos and moves the displaced head into another slot - either
		// way this offset can now name a different message. Re-check the identity under the guard rather
		// than trusting the offset: if it no longer names this CC, report a miss so the caller appends a
		// fresh entry instead of overwriting an unrelated one.
		CriticalSectionGuard guard;
		if (latest_offset >= queue_manager_.queue_count(static_cast<uint8_t>(QUEUE_PRIORITY_CC))) {
			return false;
		}
		uint32_t current = queue_manager_.read_at(static_cast<uint8_t>(QUEUE_PRIORITY_CC), latest_offset);
		if (!is_packed_channel_cc(current) || status_byte(current) != wanted_status
		    || data_1(current) != wanted_cc_number) {
			return false;
		}
		// Replace only the queued value byte at the matching offset.
		queue_manager_.overwrite_at(static_cast<uint8_t>(QUEUE_PRIORITY_CC), latest_offset,
		                            replace_data_2(current, data_2(queued_message)));
		return true;
	};

	// The shared policy scans for the latest matching CC and calls update_matched if found.
	return queue_manager_.coalesce_latest_matching_cc(wanted_status, wanted_cc_number, begin_scan, next_scan,
	                                                  update_matched);
}

bool MIDIQueueManagerUSB::enqueue_priority_message(QueuePriority priority, uint32_t queued_message) {
	// Append the packed event to the selected priority lane.
	return queue_manager_.push(static_cast<uint8_t>(priority), queued_message);
}

MIDIQueueManager::PriorityLaneTraversalResult MIDIQueueManagerUSB::handle_cc_lane(QueuePriority priority,
                                                                                  USBSendContext& context) {
	// Decide whether the CC lane head needs scheduler handling.
	uint32_t head_message = queue_manager_.head(static_cast<uint8_t>(priority));
	auto pop_scheduled_cc = [this](uint32_t& message_out) { return pop_next_scheduled_cc_message(message_out); };
	auto cc_result = MIDIQueueManager::try_pop_scheduled_cc(is_packed_channel_cc(head_message),
	                                                        context.cc_allowance_messages_remaining > 0,
	                                                        pop_scheduled_cc, context.message_out);
	if (cc_result == MIDIQueueManager::CCScheduledPopResult::Popped) {
		// Count only scheduled CC pops against the per-transfer CC allowance.
		context.cc_allowance_messages_remaining--;
		return MIDIQueueManager::PriorityLaneTraversalResult::Popped;
	}
	if (cc_result == MIDIQueueManager::CCScheduledPopResult::NotCC) {
		// A non-CC message in the CC lane can use normal head popping.
		return MIDIQueueManager::PriorityLaneTraversalResult::PopLane;
	}
	// Allowance exhaustion or pop failure means this lane should not emit now.
	return MIDIQueueManager::PriorityLaneTraversalResult::SkipLane;
}

bool MIDIQueueManagerUSB::pop_next_scheduled_cc_message(uint32_t& message_out) {
	// The generic CC policy owns selection order; USB supplies message-level scan
	// and removal callbacks so this mirrors the DIN scheduled-pop path.
	auto begin_scan = [this](uint16_t& scan_position, uint16_t& limit) {
		// Initialize a USB CC-lane scan and report how many queued events it can inspect.
		return begin_cc_message_scan(scan_position, limit);
	};
	auto next_scan = [this](uint16_t& scan_position, uint16_t limit, uint16_t& candidate_offset, uint8_t& cc_number) {
		// Read the next USB event and adapt it into the generic scheduler result shape.
		MIDIQueueManager::CCMessageScanEntry message{};
		return MIDIQueueManager::adapt_cc_candidate_scan_result(next_cc_message(scan_position, limit, message), message,
		                                                        candidate_offset, cc_number);
	};
	auto remove_selected = [this](uint16_t target_offset, uint32_t& popped_out) {
		// Remove the selected USB event and return it as the message to send.
		return remove_cc_message_at(target_offset, popped_out);
	};

	// Let the shared CC policy choose which CC number should be emitted next.
	return queue_manager_.pop_next_scheduled_cc_candidate(begin_scan, next_scan, remove_selected, message_out);
}

bool MIDIQueueManagerUSB::begin_cc_message_scan(uint16_t& scan_position, uint16_t& limit) const {
	// USB CC scan offsets are one queued event each, starting at the lane head.
	scan_position = 0;
	limit = queue_manager_.queue_count(static_cast<uint8_t>(QUEUE_PRIORITY_CC));
	return limit > 0;
}

MIDIQueueManager::CCMessageScanResult
MIDIQueueManagerUSB::next_cc_message(uint16_t& scan_position, uint16_t limit,
                                     MIDIQueueManager::CCMessageScanEntry& message) const {
	if (scan_position >= limit) {
		// The scan has consumed every logical USB event in the CC lane.
		return MIDIQueueManager::CCMessageScanResult::NoMore;
	}

	// Capture this logical offset before advancing to the next queued event.
	uint16_t offset = scan_position;
	scan_position++;
	uint32_t queued = queue_manager_.read_at(static_cast<uint8_t>(QUEUE_PRIORITY_CC), offset);
	if (!is_packed_channel_cc(queued)) {
		// Valid queued event, but not a channel CC.
		return MIDIQueueManager::CCMessageScanResult::Skip;
	}

	// Return the transport-neutral identity fields needed by the shared CC policy.
	message = {
	    .offset = offset,
	    .status = status_byte(queued),
	    .cc_number = data_1(queued),
	};
	return MIDIQueueManager::CCMessageScanResult::Found;
}

bool MIDIQueueManagerUSB::remove_cc_message_at(uint16_t target_offset, uint32_t& popped_out) {
	// USB removes one packed event from the selected logical offset.
	uint32_t removed_message[1] = {0};
	{
		// The producer's coalesce overwrite and this exchange are the only places both sides write the same
		// slot. Guarding just these few instructions closes that race; the surrounding scan deliberately
		// stays outside the guard, which is what the old clear-and-repack got wrong.
		CriticalSectionGuard guard;
		if (!queue_manager_.remove_span_via_head_swap(static_cast<uint8_t>(QUEUE_PRIORITY_CC), target_offset, 1,
		                                              removed_message)) {
			// Invalid offset: leave the caller without a message to send.
			return false;
		}
	}

	// Hand the removed event back to the transfer builder.
	popped_out = removed_message[0];
	return true;
}

/* MIDI Queue Manager DIN Transport
 *
 * This class manages the per-device DIN/serial MIDI queue with multiple priority lanes.
 * It is used by ConnectedDINMIDIDevice to stage outgoing MIDI before bytes are accepted
 * by the UART transmit buffer.
 *
 * Normal channel/system messages are encoded into raw serial MIDI bytes when queued.
 * SysEx is queued as one complete raw byte stream, all-or-nothing, in the SysEx lane.
 *
 * During MidiEngine::flushMIDI, when a DIN send transaction starts, the ConnectedDINMIDIDevice calls into this queue
 * manager to consume queued messages. The queue manager chooses complete MIDI messages or SysEx bytes according to
 * priority, DIN send allowance, and available UART space, then writes the selected bytes with bufferMIDIUart().
 *
 * After bytes are staged in the UART transmit buffer, MidiEngine calls
 * uartFlushIfNotSending() so the UART can continue sending them on the DIN port.
 *
 * After the DIN send transaction completes, future flushes call back into this queue manager to move more queued bytes
 * when pacing and UART space allow it.
 *
 * Priority lanes are drained in strict order: clock > notes > expression > CC > SysEx.
 *
 * CC messages may be coalesced into an existing queued entry instead of appended,
 * to avoid sending stale values when a later message has already superseded it.
 *
 * The queue manager also tracks whether a SysEx stream is active, and keeps draining
 * only SysEx bytes until 0xF7 is sent. This prevents interleaving other MIDI traffic
 * into a SysEx stream.
 *
 * DIN has much less bandwidth than USB, so the queue manager paces how many bytes can
 * move into the UART on each flush and limits how much low-priority CC traffic can be
 * staged ahead of later clock/note traffic.
 *
 * The queue manager does not handle UART flush state or hardware callbacks; it only
 * manages queued bytes and their priorities. MidiEngine drives the flush cadence and
 * the UART layer sends bytes after they are accepted by bufferMIDIUart().
 *
 * Message queueing flow (Normal messages):
 * Source
 *  -> MIDIMessage
 *  -> MIDICableDINPorts::sendMessage or MidiEngine::sendMidi
 *  -> MidiEngine::sendSerialMidi
 *  -> ConnectedDINMIDIDevice::enqueue_message
 *  -> MIDIQueueManagerDIN::enqueue_message
 *  -> classify_message
 *  -> message is queued into correct priority lane
 *
 * Message queueing flow (SysEx messages):
 * Source
 *  -> SysEx bytes
 *  -> MIDICableDINPorts::sendSysex
 *  -> MidiEngine::sendSerialSysex
 *  -> ConnectedDINMIDIDevice::enqueue_sysex
 *  -> MIDIQueueManagerDIN::enqueue_sysex
 *  -> message is queued into QUEUE_PRIORITY_SYSEX priority lane
 *
 * Message sending flow:
 * MidiEngine::flushMIDI
 *   -> ConnectedDINMIDIDevice::consume_queued_messages
 *   -> MIDIQueueManagerDIN::consume_queued_messages
 *   -> bufferMIDIUart
 *   -> uartFlushIfNotSending
 *   -> UART TX buffer
 *   -> UART driver
 *   -> DIN serial output sends the UART TX buffer to the connected DIN device
 */

/// Resets all DIN per-priority queues and CC scheduling bookkeeping.
void MIDIQueueManagerDIN::reset_queue_storage() {
	queue_manager_.clear_all();
	sysex_drain_active_ = false;
}

/// Resets serial pacing state so the next flush starts from a known baseline.
void MIDIQueueManagerDIN::reset_serial_state(uint32_t now_sample_timer) {
	// Start allowance accrual from the caller's current audio sample timestamp.
	serial_allowance_last_update_ = now_sample_timer;
	// Do not start with preloaded send allowance.
	serial_allowance_Q8_ = 0;
}

/// Returns whether any serial-priority lane currently has data pending.
bool MIDIQueueManagerDIN::has_serial_data() const {
	return queue_manager_.has_any_data();
}

/// Reports remaining capacity in the raw-byte SysEx queue lane.
size_t MIDIQueueManagerDIN::send_buffer_space() const {
	return queue_manager_.space(static_cast<uint8_t>(QUEUE_PRIORITY_SYSEX));
}

/// Encodes and enqueues one channel/system MIDI message into serial-priority lanes.
void MIDIQueueManagerDIN::enqueue_message(MIDIMessage message) {
	// Classify once, then let the enqueue policy decide whether to coalesce or append.
	QueuePriority priority = MIDIQueueManager::classify_message(message);
	(void)enqueue_message_with_cc_policy(priority, message);
}

/// Queues one complete SysEx byte stream into the lowest-priority DIN lane.
bool MIDIQueueManagerDIN::enqueue_sysex(uint8_t const* data, int32_t len) {
	if (data == nullptr || len < 3 || data[0] != k_midi_sysex_start_byte || data[len - 1] != k_midi_sysex_end_byte) {
		// The drain lock depends on receiving one complete SysEx stream: start byte,
		// at least one payload/ID byte, and terminating 0xF7.
		return false;
	}

	uint8_t lane = static_cast<uint8_t>(QUEUE_PRIORITY_SYSEX);
	if (static_cast<uint32_t>(queue_manager_.space(lane)) < static_cast<uint32_t>(len)) {
		// SysEx must be queued all-or-nothing so a partial stream cannot block the drain lock.
		return false;
	}

	for (int32_t i = 0; i < len; i++) {
		// DIN SysEx is already a raw byte stream, so store each byte unchanged.
		if (!queue_manager_.push(lane, data[i])) {
			// The space check above should make this unreachable unless queue state changes unexpectedly.
			return false;
		}
	}

	return true;
}

/// Drains serial-priority queues into UART while enforcing DIN pacing and strict priority gates.
void MIDIQueueManagerDIN::consume_queued_messages(uint32_t now_sample_timer) {
	if (!has_serial_data()) {
		// Fast exit when all lanes are empty; avoids pacing/space calculations.
		return;
	}

	// Apply DIN pacing before deciding this iteration's send allowance. Q8
	// units let partial serial bytes accumulate smoothly between audio callbacks.
	uint32_t delta_samples = now_sample_timer - serial_allowance_last_update_;
	if (delta_samples) {
		serial_allowance_last_update_ = now_sample_timer;
		serial_allowance_Q8_ +=
		    static_cast<int32_t>((static_cast<uint64_t>(delta_samples) * k_serial_bytes_per_second_Q8) / kSampleRate);
		if (serial_allowance_Q8_ > k_serial_send_allowance_max_Q8) {
			serial_allowance_Q8_ = k_serial_send_allowance_max_Q8;
		}
	}

	// Track total free MIDI UART capacity separately from the usable space for this flush.
	// The raw value is also used below to estimate how many non-clock bytes are already
	// staged in hardware/software UART buffering.
	int32_t raw_uart_space = uartGetTxBufferSpace(UART_ITEM_MIDI);
	int32_t uart_space = raw_uart_space - k_serial_uart_headroom_bytes;
	if (uart_space <= 0) {
		// Preserve a little headroom so other UART activity is not starved.
		return;
	}

	// Bound how much queued CC traffic is staged ahead in the UART so later
	// clock/note traffic can still preempt dense automation bursts.
	int32_t cc_uart_allowance =
	    std::max<int32_t>(0, k_serial_buffered_cc_bytes_cap - (MIDI_TX_BUFFER_SIZE - raw_uart_space));

	// Convert the accumulated Q8 send allowance to bytes available for this drain pass.
	int32_t send_allowance_bytes = serial_allowance_Q8_ >> k_serial_allowance_fraction_bits;
	constexpr size_t k_clock_idx = QUEUE_PRIORITY_CLOCK;
	if (send_allowance_bytes <= 0) {
		if (sysex_drain_active_ || queue_manager_.empty(static_cast<uint8_t>(k_clock_idx))) {
			// When the normal send allowance is zero, only realtime/system bytes
			// may bypass pacing. Active SysEx must wait so clock cannot interleave.
			return;
		}
		// Write one realtime/system byte anyway so clock/start/stop avoid extra jitter.
		send_allowance_bytes = k_midi_realtime_message_bytes;
	}

	int32_t sent = 0;
	constexpr size_t k_sysex_idx = QUEUE_PRIORITY_SYSEX;
	// Keep draining while both UART capacity and send allowance remain.
	while (uart_space > 0 && send_allowance_bytes > 0) {
		// Hold one complete outgoing message while deciding which lane can pop next.
		uint8_t bytes_to_send[k_midi_channel_message_max_bytes] = {0, 0, 0};
		QueuePriority popped_priority = QUEUE_PRIORITY_CC;
		DINSendContext context{bytes_to_send,     send_allowance_bytes, uart_space, k_midi_channel_message_max_bytes,
		                       cc_uart_allowance, popped_priority};
		bool popped = false;

		// Once a DIN SysEx stream starts, keep draining only SysEx bytes until
		// 0xF7 is sent. That preserves the MIDI rule that a SysEx stream is not
		// interleaved with other serial MIDI bytes.
		if (sysex_drain_active_) {
			popped = pop_sysex_byte(context);
			if (!popped) {
				// Defensive recovery for truncated queue state: avoid permanently
				// blocking other lanes if the SysEx lane ran dry before 0xF7.
				sysex_drain_active_ = false;
			}
		}

		if (!popped) {
			// Scan from highest to lowest active DIN priority and pop one eligible message.
			for (uint8_t lane = static_cast<uint8_t>(k_clock_idx); lane <= static_cast<uint8_t>(k_sysex_idx); lane++) {
				QueuePriority priority = static_cast<QueuePriority>(lane);
				if (!queue_manager_.queue_count(static_cast<uint8_t>(priority))) {
					// Empty lanes cannot contribute bytes this pass.
					continue;
				}

				if (priority == QUEUE_PRIORITY_CC) {
					auto cc_result = handle_cc_lane(priority, context);
					if (cc_result == MIDIQueueManager::PriorityLaneTraversalResult::Popped) {
						// The CC scheduler selected and removed a complete CC message.
						popped = true;
						break;
					}
					if (cc_result == MIDIQueueManager::PriorityLaneTraversalResult::Abort) {
						// The CC lane is blocked, malformed, or over its staging allowance.
						break;
					}
					if (cc_result == MIDIQueueManager::PriorityLaneTraversalResult::SkipLane) {
						// Skip CC for this pass and continue traversal.
						continue;
					}
					if (cc_result != MIDIQueueManager::PriorityLaneTraversalResult::PopLane) {
						// Unknown traversal result: do not pop this lane.
						continue;
					}
				}

				bool lane_popped =
				    priority == QUEUE_PRIORITY_SYSEX ? pop_sysex_byte(context) : pop_lane(priority, context);
				if (lane_popped) {
					// Lane pop succeeded; bytes_to_send now contains one transport unit.
					popped = true;
					break;
				}
			}
		}
		if (!popped) {
			// No lane could provide a complete message under the current limits.
			break;
		}

		// Reconstruct how many bytes were popped so allowances can be debited correctly.
		int32_t bytes_popped = k_midi_realtime_message_bytes;
		if (popped_priority == QUEUE_PRIORITY_SYSEX) {
			bytes_popped = k_midi_sysex_byte_count;
		}
		else if (popped_priority != QUEUE_PRIORITY_CLOCK) {
			bytes_popped = bytesPerStatusMessage(bytes_to_send[0]);
			if (bytes_popped <= 0) {
				// Defensive: a popped non-clock message should always have a valid status.
				break;
			}
		}

		bool is_cc_message = (popped_priority == QUEUE_PRIORITY_CC);
		if (is_cc_message && cc_uart_allowance < bytes_popped) {
			// Yield until the hardware drains enough queued CC traffic; clock, note, and
			// expression messages remain eligible so higher-priority output can still preempt
			// dense automation at the next flush.
			break;
		}

		for (int32_t i = 0; i < bytes_popped; i++) {
			// Push selected bytes into the UART MIDI TX buffer.
			bufferMIDIUart(bytes_to_send[i]);
		}
		sent += bytes_popped;
		if (is_cc_message) {
			// Only lowest-priority CC traffic consumes this staging cap. Higher-priority lanes
			// still use queue ordering plus available UART space, but are not blocked by CC-only
			// occupancy accounting.
			cc_uart_allowance -= bytes_popped;
		}
		// Only commit scheduling state when a full 3-byte channel-CC frame with a
		// valid CC number has actually been emitted to UART.
		if (is_cc_message && MIDIQueueManager::is_three_byte_channel_cc(bytes_to_send[0], bytes_popped)
		    && bytes_to_send[1] <= kMaxMIDIValue) {
			queue_manager_.clear_cc_debt(bytes_to_send[1]);
		}
		uart_space -= bytes_popped;
		send_allowance_bytes -= bytes_popped;
	}

	if (sent > 0) {
		// Convert sent bytes back to Q8 units and subtract them from the send allowance.
		serial_allowance_Q8_ = std::max<int32_t>(0, serial_allowance_Q8_ - sent * k_serial_allowance_fraction_scale);
	}
}

bool MIDIQueueManagerDIN::pop_lane(QueuePriority priority, DINSendContext& context) {
	if (priority == QUEUE_PRIORITY_CLOCK) {
		// The highest-priority lane is emitted one realtime/system byte at a time.
		if (context.allowance_bytes < k_midi_realtime_message_bytes
		    || context.uart_space < k_midi_realtime_message_bytes || context.max_len < k_midi_realtime_message_bytes) {
			// Caller cannot accept even the one-byte high-priority message.
			return false;
		}
		// Pop exactly one byte and mark which priority supplied it.
		bool popped = queue_manager_.pop_head(static_cast<uint8_t>(priority), context.out_bytes[0]);
		if (popped) {
			context.popped_priority = priority;
		}
		return popped;
	}

	// Non-clock DIN lanes are byte queues, so first validate that a complete
	// message is available and fits the current allowance/UART/output limits.
	uint8_t status = queue_manager_.head(static_cast<uint8_t>(priority));
	int32_t message_len = 0;
	auto head_check = MIDIQueueManager::validate_head_message_pop(
	    status, queue_manager_.queue_count(static_cast<uint8_t>(priority)), context.allowance_bytes, context.uart_space,
	    context.max_len, message_len);
	if (head_check != MIDIQueueManager::HeadMessageCheckResult::Ready) {
		return false;
	}

	// Pop the whole message atomically so partial MIDI frames are never emitted.
	bool popped = queue_manager_.pop_many(static_cast<uint8_t>(priority), context.out_bytes, message_len);
	if (popped) {
		context.popped_priority = priority;
	}
	return popped;
}

bool MIDIQueueManagerDIN::pop_sysex_byte(DINSendContext& context) {
	if (context.allowance_bytes < k_midi_sysex_byte_count || context.uart_space < k_midi_sysex_byte_count
	    || context.max_len < k_midi_sysex_byte_count) {
		// The caller cannot accept even one serial byte right now.
		return false;
	}

	// SysEx is queued as raw DIN bytes, so one pop emits exactly one byte.
	bool popped = queue_manager_.pop_head(static_cast<uint8_t>(QUEUE_PRIORITY_SYSEX), context.out_bytes[0]);
	if (popped) {
		context.popped_priority = QUEUE_PRIORITY_SYSEX;
		// Keep the drain locked until the SysEx terminator itself has been sent.
		sysex_drain_active_ = context.out_bytes[0] != k_midi_sysex_end_byte;
	}
	return popped;
}

/* **************************************************************** */
/* DIN MIDI CC specific queuing and scheduling functions start here */
/* **************************************************************** */

bool MIDIQueueManagerDIN::enqueue_message_with_cc_policy(QueuePriority priority, MIDIMessage queued_message) {
	// For CC messages, first try to update an already-queued message for the same
	// status/CC number. If this succeeds, no new serial bytes are appended.
	if (priority == QUEUE_PRIORITY_CC && coalesce_cc_message(queued_message)) {
		return true;
	}

	// Otherwise encode and append the message bytes to the selected priority lane.
	bool queued_ok = enqueue_priority_message(priority, queued_message);
	if (queued_ok && priority == QUEUE_PRIORITY_CC && queued_message.data1 <= kMaxMIDIValue) {
		// Record that this CC number has unsent work so scheduled dequeue can prefer it.
		queue_manager_.bump_cc_debt(queued_message.data1);
	}
	return queued_ok;
}

bool MIDIQueueManagerDIN::coalesce_cc_message(MIDIMessage queued_message) {
	if (!MIDIQueueManager::is_channel_cc_status_type(queued_message.statusType)) {
		// Only channel CC messages participate in value coalescing.
		return false;
	}

	// DIN coalescing overwrites only the queued value byte. Status/CC number
	// bytes stay where they are so ordering remains stable.
	uint8_t wanted_status = queued_message.channel | (queued_message.statusType << 4);
	auto begin_scan = [this](uint16_t& scan_position, uint16_t& limit) {
		// Initialize a DIN CC-lane scan and report how many raw bytes it can inspect.
		return begin_cc_message_scan(scan_position, limit);
	};
	auto next_scan = [this](uint16_t& scan_position, uint16_t limit, uint16_t& candidate_offset, uint8_t& status,
	                        uint8_t& cc_number) {
		// Read the next complete DIN message and adapt it into the generic coalescing result shape.
		MIDIQueueManager::CCMessageScanEntry message{};
		return MIDIQueueManager::adapt_cc_coalesce_scan_result(next_cc_message(scan_position, limit, message), message,
		                                                       candidate_offset, status, cc_number);
	};
	auto update_matched = [this, queued_message, wanted_status](uint16_t latest_offset) {
		// See the USB path: re-validate identity under the guard, because a concurrent removal may have
		// shifted logical offsets since the scan found this message.
		CriticalSectionGuard guard;
		uint16_t queued = queue_manager_.queue_count(static_cast<uint8_t>(QUEUE_PRIORITY_CC));
		if (static_cast<int32_t>(latest_offset) + MIDIQueueManager::k_channel_cc_message_length > queued) {
			return false;
		}
		uint8_t status = queue_manager_.read_at(static_cast<uint8_t>(QUEUE_PRIORITY_CC), latest_offset);
		uint8_t cc_number =
		    queue_manager_.read_at(static_cast<uint8_t>(QUEUE_PRIORITY_CC), static_cast<uint16_t>(latest_offset + 1));
		if (status != wanted_status || cc_number != queued_message.data1) {
			return false;
		}
		// DIN stores bytes, so the value byte is two bytes after the message status.
		queue_manager_.overwrite_at(static_cast<uint8_t>(QUEUE_PRIORITY_CC), static_cast<uint16_t>(latest_offset + 2),
		                            queued_message.data2);
		return true;
	};

	// The shared policy scans for the latest matching CC and calls update_matched if found.
	return queue_manager_.coalesce_latest_matching_cc(wanted_status, queued_message.data1, begin_scan, next_scan,
	                                                  update_matched);
}

bool MIDIQueueManagerDIN::enqueue_priority_message(QueuePriority priority, MIDIMessage queued_message) {
	// Convert the MIDIMessage container into raw serial MIDI bytes.
	uint8_t status = queued_message.channel | (queued_message.statusType << 4);
	uint8_t raw_bytes[k_midi_channel_message_max_bytes] = {status, queued_message.data1, queued_message.data2};
	int32_t message_length = bytesPerStatusMessage(status);
	if (message_length <= 0) {
		// Nothing queueable for this status.
		return true;
	}

	uint8_t lane = static_cast<uint8_t>(priority);
	if (queue_manager_.space(lane) < message_length) {
		// Do not enqueue a partial serial MIDI message.
		return false;
	}
	for (int32_t i = 0; i < message_length; i++) {
		// Store the complete message byte-by-byte in the selected priority lane.
		if (!queue_manager_.push(lane, raw_bytes[i])) {
			return false;
		}
	}
	return true;
}

// Check whether the CC lane head can be considered, then tell the caller whether to pop, skip, or abort.
MIDIQueueManager::PriorityLaneTraversalResult MIDIQueueManagerDIN::handle_cc_lane(QueuePriority priority,
                                                                                  DINSendContext& context) {
	// DIN must validate the complete message at the CC-lane head before deciding
	// whether it should be scheduled or popped normally.
	uint8_t status = queue_manager_.head(static_cast<uint8_t>(priority));
	int32_t message_len = 0;
	auto head_check = MIDIQueueManager::validate_head_message_pop(
	    status, queue_manager_.queue_count(static_cast<uint8_t>(priority)), context.allowance_bytes, context.uart_space,
	    context.max_len, message_len);
	if (head_check != MIDIQueueManager::HeadMessageCheckResult::Ready) {
		// Invalid or incomplete head data blocks the CC lane for this pass.
		return MIDIQueueManager::PriorityLaneTraversalResult::Abort;
	}

	bool head_is_cc = MIDIQueueManager::is_three_byte_channel_cc(status, message_len);
	auto pop_scheduled_cc = [this](uint8_t* out_bytes, int32_t allowance_bytes, int32_t uart_space, int32_t max_len,
	                               QueuePriority& popped_priority) {
		// Delegate scheduled removal to the DIN-specific complete-message popper.
		return pop_next_scheduled_cc_message(out_bytes, allowance_bytes, uart_space, max_len, popped_priority);
	};
	auto cc_result = MIDIQueueManager::try_pop_scheduled_cc(
	    head_is_cc, context.cc_uart_allowance >= MIDIQueueManager::k_channel_cc_message_length, pop_scheduled_cc,
	    context.out_bytes, context.allowance_bytes, context.uart_space, context.max_len, context.popped_priority);
	if (cc_result == MIDIQueueManager::CCScheduledPopResult::Popped) {
		// A scheduled CC has already been copied into the send buffer.
		return MIDIQueueManager::PriorityLaneTraversalResult::Popped;
	}
	if (cc_result == MIDIQueueManager::CCScheduledPopResult::NotCC) {
		// Non-CC messages in this lane can be emitted in normal head order.
		return MIDIQueueManager::PriorityLaneTraversalResult::PopLane;
	}

	// Allowance exhaustion or pop failure means the CC lane should not emit now.
	return MIDIQueueManager::PriorityLaneTraversalResult::Abort;
}

bool MIDIQueueManagerDIN::pop_next_scheduled_cc_message(uint8_t* out_bytes, int32_t allowance_bytes, int32_t uart_space,
                                                        int32_t max_len, QueuePriority& popped_priority) {
	if (allowance_bytes < MIDIQueueManager::k_channel_cc_message_length
	    || uart_space < MIDIQueueManager::k_channel_cc_message_length
	    || max_len < MIDIQueueManager::k_channel_cc_message_length) {
		// A DIN CC is three bytes; all caller limits must fit the complete message.
		return false;
	}

	if (queue_manager_.queue_count(static_cast<uint8_t>(QUEUE_PRIORITY_CC))
	    < MIDIQueueManager::k_channel_cc_message_length) {
		// Fewer than three queued bytes cannot form a complete channel CC.
		return false;
	}

	// DIN stores raw bytes, so scheduled CC popping must scan complete MIDI messages
	// and remove a three-byte span rather than a single queue entry.
	auto begin_scan = [this](uint16_t& scan_position, uint16_t& limit) {
		// Initialize a DIN CC-lane scan and report how many raw bytes it can inspect.
		return begin_cc_message_scan(scan_position, limit);
	};
	auto next_scan = [this](uint16_t& scan_position, uint16_t limit, uint16_t& candidate_offset, uint8_t& cc_number) {
		// Read the next complete DIN message and adapt it into the generic scheduler result shape.
		MIDIQueueManager::CCMessageScanEntry message{};
		return MIDIQueueManager::adapt_cc_candidate_scan_result(next_cc_message(scan_position, limit, message), message,
		                                                        candidate_offset, cc_number);
	};
	auto remove_selected = [this](uint16_t target_offset, uint8_t* out) {
		// Remove the selected three-byte CC message and copy it into out.
		return remove_cc_message_at(target_offset, out);
	};

	// Let the shared CC policy choose which CC number should be emitted next.
	bool popped = queue_manager_.pop_next_scheduled_cc_candidate(begin_scan, next_scan, remove_selected, out_bytes);
	if (popped) {
		// Tell the drain loop these bytes came from the CC lane.
		popped_priority = QUEUE_PRIORITY_CC;
	}
	return popped;
}

bool MIDIQueueManagerDIN::begin_cc_message_scan(uint16_t& scan_position, uint16_t& limit) const {
	// DIN CC scan offsets are byte offsets, starting at the lane head.
	scan_position = 0;
	limit = queue_manager_.queue_count(static_cast<uint8_t>(QUEUE_PRIORITY_CC));
	return limit >= MIDIQueueManager::k_channel_cc_message_length;
}

MIDIQueueManager::CCMessageScanResult
MIDIQueueManagerDIN::next_cc_message(uint16_t& scan_position, uint16_t limit,
                                     MIDIQueueManager::CCMessageScanEntry& message) const {
	if (scan_position >= limit) {
		// The scan has consumed every raw byte in the CC lane.
		return MIDIQueueManager::CCMessageScanResult::NoMore;
	}

	// Decode the message length from the status byte at this logical byte offset.
	uint8_t message_status = queue_manager_.read_at(static_cast<uint8_t>(QUEUE_PRIORITY_CC), scan_position);
	int32_t message_len = bytesPerStatusMessage(message_status);
	if (message_len <= 0 || static_cast<int32_t>(scan_position) + message_len > limit) {
		// A truncated or unparseable byte stream means the lane cannot safely be
		// repacked around a selected message.
		return MIDIQueueManager::CCMessageScanResult::Invalid;
	}

	// Save the starting byte offset, then advance to the next complete MIDI message.
	uint16_t offset = scan_position;
	scan_position = static_cast<uint16_t>(scan_position + message_len);
	if (!MIDIQueueManager::is_three_byte_channel_cc(message_status, message_len)) {
		// Valid queued message, but not a three-byte channel CC.
		return MIDIQueueManager::CCMessageScanResult::Skip;
	}

	// Return the transport-neutral identity fields needed by the shared CC policy.
	message = {
	    .offset = offset,
	    .status = message_status,
	    .cc_number = queue_manager_.read_at(static_cast<uint8_t>(QUEUE_PRIORITY_CC), static_cast<uint16_t>(offset + 1)),
	};
	return MIDIQueueManager::CCMessageScanResult::Found;
}

bool MIDIQueueManagerDIN::remove_cc_message_at(uint16_t target_offset, uint8_t* out) {
	// DIN removes a three-byte CC span starting at the selected byte offset.
	// See the USB path: narrow guard around the only slot writes shared with the producer.
	CriticalSectionGuard guard;
	return queue_manager_.remove_span_via_head_swap(static_cast<uint8_t>(QUEUE_PRIORITY_CC), target_offset,
	                                                MIDIQueueManager::k_channel_cc_message_length, out);
}
