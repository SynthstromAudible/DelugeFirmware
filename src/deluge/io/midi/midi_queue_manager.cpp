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
#include "timers_interrupts/timers_interrupts.h"

/// Set when this queue has data waiting so the engine's flush logic knows to schedule a transfer.
/// Declared here rather than including midi_engine.h: this is a shared flag, and the queue manager no
/// longer calls into the engine.
extern bool anythingInUSBOutputBuffer;

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

bool MIDIQueueManagerUSB::has_buffered_send_data() const {
	// True when at least one queued USB message exists across any priority lane.
	return queue_storage_.total_queued_messages() > 0;
}

int MIDIQueueManagerUSB::send_buffer_space() const {
	// Total queued USB messages currently buffered across all priority lanes.
	uint32_t queued = queue_storage_.total_queued_messages();
	// Maximum messages we can queue: usable slots per lane (ring-1) times number of lanes.
	uint32_t total_capacity_messages = 0;
	for (uint8_t lane = 0; lane < QUEUE_PRIORITY_COUNT; lane++) {
		// Each lane keeps one slot unused, and lanes no longer share a capacity.
		total_capacity_messages += queue_storage_.lane_capacity(lane) - 1;
	}
	// Can't queue anymore: return 0 bytes of remaining capacity.
	if (queued >= total_capacity_messages) {
		return 0;
	}

	// Each 4-byte USB-MIDI event contains up to 3 bytes of MIDI payload.
	// Report remaining capacity by payload bytes, not by 4-byte USB event slots.
	return (total_capacity_messages - queued) * k_usb_midi_event_payload_bytes;
}

QueuePriority MIDIQueueManagerUSB::classify_packed_usb_priority(uint32_t packed, MIDIIntent intent) {
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
	    .intent = intent,
	};
	return MIDIQueueManager::classify_message(decoded);
}

bool MIDIQueueManagerUSB::enqueue_message(uint32_t full_message, MIDIIntent intent) {
	// Total messages currently queued across all priority lanes for this device.
	uint32_t queued = queue_storage_.total_queued_messages();
	// Report backlog rather than acting on it. Flushing from here would call back into the engine that
	// owns this queue, and would let a mainline enqueue trigger the interrupt-masked drain.
	bool wants_flush = queued > k_usb_flush_backlog_message_threshold;

	// Determine which priority lane this packed USB-MIDI event belongs to.
	QueuePriority priority = classify_packed_usb_priority(full_message, intent);
	// Occupancy of just the selected priority lane we are about to enqueue into.
	uint16_t queue_size = queue_storage_.queue_count(static_cast<uint8_t>(priority));
	// Keep one slot free in each ring so full/empty states stay distinguishable.
	if (queue_size >= queue_storage_.lane_capacity(static_cast<uint8_t>(priority)) - 1) {
		// Full: drop this message rather than overwrite unread queued data, and ask the caller to flush
		// so the next one finds space.
		// TODO: show some error message
		return true;
	}

	// CC messages may be coalesced into an existing queued entry instead of appended. The policy is
	// shared with DIN; UsbTransport supplies everything about this transport's storage format.
	bool queued_ok = cc_lane_.enqueue_with_cc_policy(
	    queue_storage_.lanes[static_cast<uint8_t>(QUEUE_PRIORITY_CC)], cc_policy_, priority == QUEUE_PRIORITY_CC,
	    is_packed_channel_cc(full_message), status_byte(full_message), data_1(full_message), data_2(full_message),
	    [this, priority, full_message] { return enqueue_priority_message(priority, full_message); });

	// Signal that at least one USB message is waiting so flush logic can schedule transmission.
	if (queued_ok) {
		anythingInUSBOutputBuffer = true;
	}
	return wants_flush;
}

// Drains queued USB messages into the smaller `dataSendingNow` transfer buffer.
bool MIDIQueueManagerUSB::consume_queued_messages(uint8_t* data_sending_now, uint8_t& num_bytes_sending_now,
                                                  bool usb_host_mode) {
	// Snapshot total queued messages across all priority lanes.
	uint32_t queued = queue_storage_.total_queued_messages();
	if (queued == 0) {
		// Nothing pending: caller should not start a USB send transfer.
		return false;
	}

	int32_t i = 0;
	// Many devices do not accept more than 64 bytes of data at a time (this may be
	// inferable from device metadata). Some accept even less, especially through hubs:
	// the Hydrasynth only responds to a max of 2 messages per transfer, and the third
	// gets blocked. For MPE this leads to note-ons being ignored, since the X/Y resets
	// are sent before the note-on.
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
			if (!queue_storage_.queue_count(static_cast<uint8_t>(priority))) {
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
	return queue_storage_.pop_head(static_cast<uint8_t>(priority), context.message_out);
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

bool MIDIQueueManagerUSB::enqueue_priority_message(QueuePriority priority, uint32_t queued_message) {
	// Append the packed event to the selected priority lane.
	return queue_storage_.push(static_cast<uint8_t>(priority), queued_message);
}

MIDIQueueManager::PriorityLaneTraversalResult MIDIQueueManagerUSB::handle_cc_lane(QueuePriority priority,
                                                                                  USBSendContext& context) {
	// Decide whether the CC lane head needs scheduler handling.
	uint32_t head_message = queue_storage_.head(static_cast<uint8_t>(priority));
	auto pop_scheduled_cc = [this](uint32_t& message_out) {
		// The shared policy owns selection order and the guarded removal; USB adds nothing here.
		return cc_lane_.pop_scheduled(queue_storage_.lanes[static_cast<uint8_t>(QUEUE_PRIORITY_CC)], cc_policy_,
		                              &message_out);
	};
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

// Resets serial pacing state so the next flush starts from a known baseline.
void MIDIQueueManagerDIN::reset_serial_state(uint32_t now_sample_timer) {
	// Start allowance accrual from the caller's current audio sample timestamp.
	serial_allowance_last_update_ = now_sample_timer;
	// Do not start with preloaded send allowance.
	serial_allowance_Q8_ = 0;
}

// Returns whether any serial-priority lane currently has data pending.
bool MIDIQueueManagerDIN::has_serial_data() const {
	return queue_storage_.total_queued_messages() > 0;
}

// Reports remaining capacity in the raw-byte SysEx queue lane.
size_t MIDIQueueManagerDIN::send_buffer_space() const {
	return queue_storage_.space(static_cast<uint8_t>(QUEUE_PRIORITY_SYSEX));
}

// Encodes and enqueues one channel/system MIDI message into serial-priority lanes.
void MIDIQueueManagerDIN::enqueue_message(MIDIMessage message) {
	// Classify once, then let the enqueue policy decide whether to coalesce or append. The policy is
	// shared with USB; DinTransport supplies everything about this transport's storage format.
	QueuePriority priority = MIDIQueueManager::classify_message(message);
	uint8_t status = static_cast<uint8_t>(message.channel | (message.statusType << 4));
	(void)cc_lane_.enqueue_with_cc_policy(
	    queue_storage_.lanes[static_cast<uint8_t>(QUEUE_PRIORITY_CC)], cc_policy_, priority == QUEUE_PRIORITY_CC,
	    MIDIQueueManager::is_channel_cc_status_type(message.statusType), status, message.data1, message.data2,
	    [this, priority, message] { return enqueue_priority_message(priority, message); });
}

// Queues one complete SysEx byte stream into the lowest-priority DIN lane.
bool MIDIQueueManagerDIN::enqueue_sysex(uint8_t const* data, int32_t len) {
	if (data == nullptr || len < 3 || data[0] != k_midi_sysex_start_byte || data[len - 1] != k_midi_sysex_end_byte) {
		// The drain lock depends on receiving one complete SysEx stream: start byte,
		// at least one payload/ID byte, and terminating 0xF7.
		return false;
	}

	uint8_t lane = static_cast<uint8_t>(QUEUE_PRIORITY_SYSEX);
	if (static_cast<uint32_t>(queue_storage_.space(lane)) < static_cast<uint32_t>(len)) {
		// SysEx must be queued all-or-nothing so a partial stream cannot block the drain lock.
		return false;
	}

	for (int32_t i = 0; i < len; i++) {
		// DIN SysEx is already a raw byte stream, so store each byte unchanged.
		if (!queue_storage_.push(lane, data[i])) {
			// The space check above should make this unreachable unless queue state changes unexpectedly.
			return false;
		}
	}

	return true;
}

// Drains serial-priority queues into UART while enforcing DIN pacing and strict priority gates.
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
		if (sysex_drain_active_ || queue_storage_.empty(static_cast<uint8_t>(k_clock_idx))) {
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
				if (!queue_storage_.queue_count(static_cast<uint8_t>(priority))) {
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
						// The CC lane head is malformed or incomplete, so it cannot safely provide bytes.
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
			cc_policy_.clear_cc_debt(bytes_to_send[1]);
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
		bool popped = queue_storage_.pop_head(static_cast<uint8_t>(priority), context.out_bytes[0]);
		if (popped) {
			context.popped_priority = priority;
		}
		return popped;
	}

	// Non-clock DIN lanes are byte queues, so first validate that a complete
	// message is available and fits the current allowance/UART/output limits.
	uint8_t status = queue_storage_.head(static_cast<uint8_t>(priority));
	int32_t message_len = 0;
	auto head_check = MIDIQueueManager::validate_head_message_pop(
	    status, queue_storage_.queue_count(static_cast<uint8_t>(priority)), context.allowance_bytes, context.uart_space,
	    context.max_len, message_len);
	if (head_check != MIDIQueueManager::HeadMessageCheckResult::Ready) {
		return false;
	}

	// Pop the whole message atomically so partial MIDI frames are never emitted.
	bool popped = queue_storage_.pop_many(static_cast<uint8_t>(priority), context.out_bytes, message_len);
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
	bool popped = queue_storage_.pop_head(static_cast<uint8_t>(QUEUE_PRIORITY_SYSEX), context.out_bytes[0]);
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
	if (queue_storage_.space(lane) < message_length) {
		// Do not enqueue a partial serial MIDI message.
		return false;
	}
	for (int32_t i = 0; i < message_length; i++) {
		// Store the complete message byte-by-byte in the selected priority lane.
		if (!queue_storage_.push(lane, raw_bytes[i])) {
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
	uint8_t status = queue_storage_.head(static_cast<uint8_t>(priority));
	int32_t message_len = 0;
	auto head_check = MIDIQueueManager::validate_head_message_pop(
	    status, queue_storage_.queue_count(static_cast<uint8_t>(priority)), context.allowance_bytes, context.uart_space,
	    context.max_len, message_len);
	if (head_check != MIDIQueueManager::HeadMessageCheckResult::Ready) {
		// Invalid or incomplete head data blocks the CC lane for this pass.
		return MIDIQueueManager::PriorityLaneTraversalResult::Abort;
	}

	bool head_is_cc = MIDIQueueManager::is_three_byte_channel_cc(status, message_len);
	auto pop_scheduled_cc = [this](uint8_t* out_bytes, int32_t allowance_bytes, int32_t uart_space, int32_t max_len,
	                               QueuePriority& popped_priority) {
		if (allowance_bytes < MIDIQueueManager::k_channel_cc_message_length
		    || uart_space < MIDIQueueManager::k_channel_cc_message_length
		    || max_len < MIDIQueueManager::k_channel_cc_message_length) {
			// A DIN CC is three bytes; all caller limits must fit the complete message. This gate is
			// DIN's, not the policy's: USB pops whole events and has nothing to check here.
			return false;
		}
		// The shared policy owns selection order and the guarded removal, and its own scan refuses a lane
		// holding fewer bytes than one complete message.
		if (!cc_lane_.pop_scheduled(queue_storage_.lanes[static_cast<uint8_t>(QUEUE_PRIORITY_CC)], cc_policy_,
		                            out_bytes)) {
			return false;
		}
		// Tell the drain loop these bytes came from the CC lane.
		popped_priority = QUEUE_PRIORITY_CC;
		return true;
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

	if (cc_result == MIDIQueueManager::CCScheduledPopResult::AllowanceBlocked) {
		// Blocked by the CC send allowance, not by bad data. Fall through to lower-priority lanes, as
		// USB does for the same condition, instead of halting the whole pass.
		return MIDIQueueManager::PriorityLaneTraversalResult::SkipLane;
	}
	// A scheduled pop that failed on a head this transport could not decode cannot be retried safely.
	return MIDIQueueManager::PriorityLaneTraversalResult::Abort;
}
