/*
 * Copyright © 2026 Sean Ditny and Katherine Whitlock
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

#include "io/midi/midi_cc_policy.h"
#include "io/midi/midi_queue_definitions.h"
#include "io/midi/midi_queue_lane.h"
#include "io/midi/midi_queue_transports.h"
#include "model/midi/message.h"
#include <cstdint>

/// @brief USB transport queue manager: packs, prioritizes, and drains outgoing USB-MIDI events.
///
/// Storage unit is one packed USB-MIDI event (uint32_t) per queue entry.
///
/// @note Owns queued messages and their priorities only. It performs no USB transfer logic and handles
///       no driver callbacks; ConnectedUSBMIDIDevice runs the send transactions and calls in to fill
///       each transfer buffer.
class MIDIQueueManagerUSB {
public:
	/// @brief Clears USB queue contents and CC scheduling bookkeeping for this device.
	void reset_queue_storage() {
		// Drop queued transport data from every priority lane.
		queue_storage_.clear();
		// Reset scheduler state so stale debt/scan data does not survive a device reset.
		cc_policy_.reset();
		sysex_drain_active_ = false;
	}
	/// @brief Returns whether any USB priority lane has data waiting to send.
	/// @return True if at least one lane is non-empty.
	[[nodiscard]] bool has_buffered_send_data() const;
	/// @brief Returns remaining USB SysEx-lane capacity, for the SysEx display throttle.
	/// @return Free space in the SysEx lane only, reported as MIDI payload bytes rather than 4-byte
	///         USB event slots. Not an aggregate across lanes: SysEx cannot occupy any other lane, so
	///         an aggregate would hide a full SysEx lane from the caller that has to back off.
	[[nodiscard]] int send_buffer_space() const;
	/// @brief Queues one packed USB-MIDI event, classifying it into the correct priority lane.
	/// @param full_message Packed USB-MIDI event.
	/// @param intent       Sender intent used to route Event vs. Continuous CCs into the correct lane.
	/// @return True when the queued backlog has grown past the flush threshold and the caller should
	///         flush. The queue manager deliberately does not flush itself: doing so would close a call
	///         cycle (engine -> device -> queue -> engine) between layers that are otherwise one-way.
	///         Both callers do flush synchronously on a true return, so this does not change *when* the
	///         drain runs, only who calls it.
	[[nodiscard]] bool enqueue_message(uint32_t full_message, MIDIIntent intent);
	/// @brief Drains queued USB-MIDI events into the USB send buffer in priority order.
	/// @param data_sending_now      Destination buffer for packed events to send.
	/// @param num_bytes_sending_now Out: number of bytes written to @p data_sending_now.
	/// @param usb_host_mode         True when operating as USB host rather than USB device.
	/// @return True if at least one message was written.
	bool consume_queued_messages(uint8_t* data_sending_now, uint8_t& num_bytes_sending_now, bool usb_host_mode);

	/// @brief Per-pop output destination and remaining CC scheduling allowance for one USB drain call.
	struct USBSendContext {
		/// Out: the popped, packed USB-MIDI event.
		uint32_t& message_out;
		/// Remaining scheduled-CC pops allowed this call.
		int32_t& cc_allowance_messages_remaining;
	};

private:
	/// @brief Per-priority USB output queues.
	///
	/// Each lane is a ring of packed USB-MIDI events; consume_queued_messages() drains them into
	/// dataSendingNow in priority order.
	MIDIQueueStorage<uint32_t, QUEUE_PRIORITY_COUNT, k_usb_lane_capacity> queue_storage_{};
	/// @brief Per-device CC coalescing/scheduling bookkeeping layered on top of the queue storage.
	MIDICCQueuePolicy cc_policy_{};
	/// @brief The CC-lane policy, shared with DIN and specialised only by the USB transport traits.
	MIDICCLanePolicy<UsbTransport> cc_lane_{};

	/// @brief Classifies a packed outgoing USB-MIDI message into a priority lane.
	/// @param packed Packed USB-MIDI event.
	/// @param intent Sender intent used to route Event vs. Continuous CCs.
	/// @return The priority lane this message belongs in.
	[[nodiscard]] static QueuePriority classify_packed_usb_priority(uint32_t packed, MIDIIntent intent);
	/// @brief Pops one queued message according to strict USB priority ordering.
	/// @param priority Priority lane to pop from.
	/// @param context  Output destination and CC scheduling allowance for this pop.
	/// @return True if a message was popped.
	bool pop_lane(QueuePriority priority, USBSendContext& context);

	/// @brief Appends one packed USB-MIDI event to its priority lane.
	/// @param priority       Target priority lane.
	/// @param queued_message Packed USB-MIDI event to enqueue.
	/// @return True if the event was stored; false if that lane is full.
	[[nodiscard]] bool enqueue_priority_message(QueuePriority priority, uint32_t queued_message);
	/// @brief Decides how to advance CC lane traversal during a USB dequeue pass.
	/// @param priority Priority lane under consideration.
	/// @param context  Output destination and CC scheduling allowance for this pop.
	/// @return How the caller should proceed for this lane.
	[[nodiscard]] MIDIQueueManager::PriorityLaneTraversalResult handle_cc_lane(QueuePriority priority,
	                                                                           USBSendContext& context);
	/// @brief Pops one queued SysEx event and keeps USB drain locked to SysEx until the ending event is sent.
	/// @param context Output destination and CC scheduling allowance for this pop.
	/// @return True if a SysEx event was popped.
	[[nodiscard]] bool pop_sysex_message(USBSendContext& context);
	/// @brief True after a USB SysEx start/continue event has been popped but before its terminating
	/// event has been sent.
	bool sysex_drain_active_{false};
};

/// @brief DIN transport queue manager: packs, paces, and drains outgoing serial MIDI bytes.
///
/// Storage unit is one raw serial byte per queue entry (as opposed to USB's packed uint32 events).
/// Channel and system messages are encoded to raw bytes at enqueue time; SysEx is queued all-or-nothing
/// as one complete stream.
///
/// @note DIN carries far less bandwidth than USB, so this manager paces how many bytes may move into the
///       UART per flush and caps how much low-priority CC traffic can be staged ahead of clock and note
///       traffic that has not arrived yet.
/// @note Owns queued bytes and their priorities only. It holds no UART flush state and handles no
///       hardware callbacks; MidiEngine drives the flush cadence, and the UART layer transmits once
///       bufferMIDIUart() has accepted the bytes.
class MIDIQueueManagerDIN {
public:
	/// @brief Clears DIN queue contents and CC scheduling bookkeeping for this device.
	void reset_queue_storage() {
		// Drop queued transport data from every priority lane.
		queue_storage_.clear();
		// Reset scheduler state so stale debt/scan data does not survive a device reset.
		cc_policy_.reset();
		sysex_drain_active_ = false;
	}
	/// @brief Resets serial queue pacing state to a known baseline.
	/// @param now_sample_timer Current sample-timer tick.
	void reset_serial_state(uint32_t now_sample_timer);
	/// @brief Returns whether any serial-priority lane has pending bytes.
	/// @return True if at least one lane is non-empty.
	[[nodiscard]] bool has_serial_data() const;
	/// @brief Remaining DIN queue capacity for raw SysEx bytes.
	/// @return Free bytes in the SysEx lane only, not an aggregate across lanes.
	[[nodiscard]] size_t send_buffer_space() const;
	/// @brief Queues one channel/system MIDI message into the serial-priority queues.
	/// @param message Message to enqueue.
	void enqueue_message(MIDIMessage message);
	/// @brief Queues one complete SysEx byte stream into the serial-priority queues.
	/// @param data Pointer to the SysEx byte stream, including its 0xF0/0xF7 framing.
	/// @param len  Length of @p data, in bytes.
	/// @return True if the stream was queued; false if it did not fit.
	bool enqueue_sysex(uint8_t const* data, int32_t len);
	/// @brief Drains serial-priority queues into UART under pacing and priority rules.
	/// @param now_sample_timer Current sample-timer tick, used to accrue send allowance.
	void consume_queued_messages(uint32_t now_sample_timer);

	/// @brief Per-pop output destination, pacing allowances, and popped-lane result for one DIN drain call.
	struct DINSendContext {
		/// Destination buffer for bytes to send.
		uint8_t* out_bytes;
		/// Remaining caller send allowance, in bytes.
		int32_t allowance_bytes;
		/// Remaining space in the UART output buffer.
		int32_t uart_space;
		/// Caller-imposed maximum message length for this pop.
		int32_t max_len;
		/// Remaining scheduled-CC send allowance, in bytes.
		int32_t cc_uart_allowance;
		/// Out: priority lane the popped message/byte came from.
		QueuePriority& popped_priority;
	};

private:
	/// @brief Number of active serial-priority lanes [clock..SysEx] scanned during dequeue.
	static constexpr size_t k_serial_priority_count = QUEUE_PRIORITY_COUNT;
	/// @brief Per-priority byte rings holding pending DIN output grouped by queue policy.
	///
	/// Capacities come from k_din_lane_capacity, sized per lane rather than uniformly. The SysEx lane is
	/// larger than MIDI_TX_BUFFER_SIZE so a full 1024-byte stream fits despite the one-unused-slot
	/// invariant.
	MIDIQueueStorage<uint8_t, k_serial_priority_count, k_din_lane_capacity> queue_storage_{};
	/// @brief Per-device CC coalescing/scheduling bookkeeping layered on top of the queue storage.
	MIDICCQueuePolicy cc_policy_{};
	/// @brief The CC-lane policy, shared with USB and specialised only by the DIN transport traits.
	MIDICCLanePolicy<DinTransport> cc_lane_{};
	/// @brief Last sample-timer tick used to accrue DIN send allowance.
	uint32_t serial_allowance_last_update_{0};
	/// @brief Accumulated DIN send allowance in Q8 bytes (8 fractional bits).
	int32_t serial_allowance_Q8_{0};
	/// @brief True after DIN begins draining a SysEx byte stream and before 0xF7 is sent.
	bool sysex_drain_active_{false};

	/// @brief Pops one realtime/system byte or one complete MIDI message according to lane priority.
	/// @param priority Priority lane to pop from.
	/// @param context  Output destination, pacing allowances, and popped-lane result.
	/// @return True if a byte or message was popped.
	bool pop_lane(QueuePriority priority, DINSendContext& context);
	/// @brief Pops one queued SysEx byte and keeps DIN drain locked to SysEx until 0xF7 is sent.
	/// @param context Output destination, pacing allowances, and popped-lane result.
	/// @return True if a SysEx byte was popped.
	bool pop_sysex_byte(DINSendContext& context);

	/// @brief Encodes one MIDIMessage to serial bytes and appends it to a priority lane.
	/// @param priority       Target priority lane.
	/// @param queued_message Message to enqueue.
	/// @return True if the message was stored; false if that lane is full.
	[[nodiscard]] bool enqueue_priority_message(QueuePriority priority, MIDIMessage queued_message);
	/// @brief Decides how to advance CC lane traversal during a DIN dequeue pass.
	/// @param priority Priority lane under consideration.
	/// @param context  Output destination, pacing allowances, and popped-lane result.
	/// @return How the caller should proceed for this lane.
	[[nodiscard]] MIDIQueueManager::PriorityLaneTraversalResult handle_cc_lane(QueuePriority priority,
	                                                                           DINSendContext& context);
};
