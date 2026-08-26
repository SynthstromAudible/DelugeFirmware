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
#include "io/midi/midi_cc_policy.h"
#include "io/midi/midi_queue_definitions.h"
#include "io/midi/midi_queue_transports.h"
#include "model/midi/message.h"
#include <array>
#include <cstdint>

/// @brief Power-of-two ring buffer lane shared by transport-specific queue managers.
///
/// @warning Capacity MUST be a power of two (checked by MIDIQueueStorage): positions wrap with a
///          bitmask, not a modulo by an arbitrary size.
/// @note The lane always keeps one slot unused so `read_pos == write_pos` can unambiguously mean
///       empty. Usable capacity is therefore `capacity() - 1`. All logical offsets passed to and
///       returned from this class are relative to `read_pos`, which lets callers scan or overwrite
///       queued messages without exposing wrapped physical indices.
/// @note A view, not an owner. MIDIQueueStorage owns one flat pool and hands each lane its slice, so
///       lanes can have different capacities without becoming different types.
template <typename T>
class MIDIQueueLane {
public:
	/// @brief Backing storage for this lane, owned by MIDIQueueStorage.
	T* data{nullptr};
	/// @brief `capacity() - 1`. Positions wrap with this mask, so capacity must be a power of two.
	uint16_t mask{0};

	/// @brief Total slots in this lane, one of which is always kept unused.
	[[nodiscard]] uint16_t capacity() const { return static_cast<uint16_t>(mask + 1); }

	/// @brief Physical index of the oldest queued entry.
	uint16_t read_pos{0};
	/// @brief Physical index of the next slot to write.
	///
	/// @warning Owned by the producer. See remove_span_via_head_swap() for why the consumer must never
	///          write this field directly.
	uint16_t write_pos{0};

	/// @brief Returns whether the lane holds no queued entries.
	[[nodiscard]] bool empty() const { return read_pos == write_pos; }
	/// @brief Returns the number of queued entries.
	[[nodiscard]] uint16_t size() const { return static_cast<uint16_t>((write_pos - read_pos) & mask); }
	/// @brief Returns the number of additional entries that can be pushed before the lane is full.
	[[nodiscard]] uint16_t space() const { return static_cast<uint16_t>(mask - size()); }
	/// @brief Reads a queued entry without removing it.
	/// @param offset Logical offset from `read_pos`; 0 is the lane head.
	/// @return The entry at @p offset.
	[[nodiscard]] T peek(uint16_t offset = 0) const { return data[(read_pos + offset) & mask]; }

	/// @brief Appends one entry to the tail of the lane.
	/// @param value Entry to enqueue.
	/// @return True if the entry was stored; false if the lane is full (usable capacity is `capacity() - 1`).
	bool push(T value) {
		// Advance write position first so we can detect the one-unused-slot full case.
		uint16_t next = static_cast<uint16_t>((write_pos + 1) & mask);
		if (next == read_pos) {
			return false;
		}
		// Store into the current write slot, then publish it by moving write_pos.
		data[write_pos] = value;
		write_pos = next;
		return true;
	}

	/// @brief Removes and returns the oldest queued entry.
	/// @param out Out: set to the removed entry on success.
	/// @return True if an entry was popped; false if the lane was empty.
	bool pop(T& out) {
		if (empty()) {
			return false;
		}
		// Copy the oldest queued entry before advancing the read position.
		out = data[read_pos];
		read_pos = static_cast<uint16_t>((read_pos + 1) & mask);
		return true;
	}

	/// @brief Removes @p count queued entries as one atomic span, oldest first.
	/// @param out   Destination buffer; must hold at least @p count entries.
	/// @param count Number of entries to pop.
	/// @return True if all @p count entries were popped; false (with no state change) if fewer were queued.
	bool pop_many(T* out, uint16_t count) {
		if (size() < count) {
			// The caller asked for an atomic span; do not partially pop it.
			return false;
		}
		// Copy the contiguous logical span, even if the physical ring wraps.
		for (uint16_t i = 0; i < count; i++) {
			out[i] = data[(read_pos + i) & mask];
		}
		// Commit the pop only after all requested entries were copied.
		read_pos = static_cast<uint16_t>((read_pos + count) & mask);
		return true;
	}

	/// @brief Removes a logical span by exchanging it with the head, then dropping the head.
	///
	/// Scheduled CC dequeue can select an entry that is not at the lane head. Two properties matter here:
	///
	/// @warning `write_pos` is owned by the producer, and `consume_queued_messages()` runs in an ISR
	///          (see the warnings in midi_engine.cpp) - so the consumer must never write it. Clearing
	///          and rebuilding the ring to remove a mid-lane span would race a mainline `push()` and
	///          could lose or reorder queued MIDI. Swapping with the head instead touches only
	///          `read_pos` and the two spans being exchanged.
	/// @note The freed slot is reclaimed immediately. Marking the span dead in place instead would leak
	///       one slot per out-of-order removal until it reached the head, which under sustained CC
	///       automation fills the lane and starts dropping messages.
	///
	/// Scheduled removal only runs when the head is itself a three-byte channel CC, so the head span and
	/// the selected span are always the same width. The entry displaced from the head moves to the
	/// selected position; CC entries are independent of one another, and this feature already reorders
	/// them by design, so that is within the ordering the scheduler is allowed to produce.
	///
	/// @param target_offset Logical offset of the span to remove, relative to `read_pos`.
	/// @param span          Width of the span, in entries.
	/// @param removed_out   Destination buffer for the removed entries; must hold at least @p span entries.
	/// @return True if the span was removed; false if it falls outside the current logical queue contents.
	bool remove_span_via_head_swap(uint16_t target_offset, uint16_t span, T* removed_out) {
		uint16_t queue_size = size();
		if (span > queue_size || target_offset > static_cast<uint16_t>(queue_size - span)) {
			// The requested span is outside the current logical queue contents.
			return false;
		}

		for (uint16_t i = 0; i < span; i++) {
			uint16_t head_index = static_cast<uint16_t>((read_pos + i) & mask);
			uint16_t selected_index = static_cast<uint16_t>((read_pos + target_offset + i) & mask);
			// Copy the selected entry out, then move the head entry into the slot it vacated.
			removed_out[i] = data[selected_index];
			data[selected_index] = data[head_index];
		}

		// Dropping the head is what actually frees the slot. When target_offset is 0 this degenerates to a
		// plain pop, which is exactly right.
		read_pos = static_cast<uint16_t>((read_pos + span) & mask);
		return true;
	}

	/// @brief Empties the lane by resetting both positions to zero.
	void clear() {
		// With this ring representation, equal positions mean empty.
		read_pos = 0;
		write_pos = 0;
	}

	/// @brief Overwrites a queued entry in place without changing queue occupancy.
	/// @param logical_offset Logical offset from `read_pos` of the entry to overwrite.
	/// @param value          New value for that entry.
	void overwrite_at(uint16_t logical_offset, T value) { data[(read_pos + logical_offset) & mask] = value; }
};

/// @brief Fixed set of power-of-two queue lanes shared by a transport-specific manager.
template <typename T, size_t LaneCount, uint16_t const (&Capacities)[LaneCount]>
class MIDIQueueStorage {
public:
	/// @brief The priority lanes, indexed by QueuePriority.
	std::array<MIDIQueueLane<T>, LaneCount> lanes{};

	/// @brief Points each lane at its slice of the pool and sets its wrap mask.
	MIDIQueueStorage() {
		uint32_t offset = 0;
		for (size_t i = 0; i < LaneCount; i++) {
			lanes[i].data = pool.data() + offset;
			lanes[i].mask = static_cast<uint16_t>(Capacities[i] - 1);
			offset += Capacities[i];
		}
	}

	/// @brief Total slots in one lane, one of which is always kept unused.
	/// @param lane Priority lane index.
	/// @return That lane's capacity.
	[[nodiscard]] uint16_t lane_capacity(uint8_t lane) const { return lanes[lane].capacity(); }

	/// @name Per-lane accessors
	/// @{

	/// @brief Returns the number of queued entries in one lane.
	/// @param lane Priority lane index.
	/// @return Entries currently queued in that lane.
	[[nodiscard]] uint16_t queue_count(uint8_t lane) const { return lanes[lane].size(); }
	/// @brief Returns the total number of entries queued across all lanes.
	/// @return Sum of every lane's queued entry count.
	[[nodiscard]] uint32_t total_queued_messages() const {
		uint32_t queued = 0;
		for (auto const& queue_lane : lanes) {
			// Sum all priority lanes so callers can make whole-device decisions.
			queued += queue_lane.size();
		}
		return queued;
	}

	/// @brief Reads one lane's head entry without removing it.
	/// @param lane Priority lane index.
	/// @return The entry at the lane head.
	[[nodiscard]] T head(uint8_t lane) const { return lanes[lane].peek(); }
	/// @brief Removes and returns one lane's head entry.
	/// @param lane Priority lane index.
	/// @param out  Out: set to the removed entry on success.
	/// @return True if an entry was popped; false if that lane was empty.
	bool pop_head(uint8_t lane, T& out) { return lanes[lane].pop(out); }
	/// @brief Removes @p count entries from one lane as one atomic span, oldest first.
	/// @param lane  Priority lane index.
	/// @param out   Destination buffer; must hold at least @p count entries.
	/// @param count Number of entries to pop.
	/// @return True if all @p count entries were popped; false (with no state change) if fewer were queued.
	bool pop_many(uint8_t lane, T* out, uint16_t count) { return lanes[lane].pop_many(out, count); }
	/// @brief Appends one entry to one lane.
	/// @param lane  Priority lane index.
	/// @param value Entry to enqueue.
	/// @return True if the entry was stored; false if that lane is full.
	bool push(uint8_t lane, T value) { return lanes[lane].push(value); }
	/// @brief Returns whether one lane holds no queued entries.
	/// @param lane Priority lane index.
	/// @return True if that lane is empty.
	[[nodiscard]] bool empty(uint8_t lane) const { return lanes[lane].empty(); }
	/// @brief Returns how many more entries can be pushed to one lane before it is full.
	/// @param lane Priority lane index.
	/// @return Remaining usable capacity of that lane.
	[[nodiscard]] uint16_t space(uint8_t lane) const { return lanes[lane].space(); }

	/// @}

	/// @brief Empties every lane.
	///
	/// @note Storage only. Policy state layered above the lanes (CC debt, scheduling bookkeeping) is
	///       owned elsewhere and must be reset by its owner.
	void clear() {
		for (auto& queue_lane : lanes) {
			// Drop queued transport data from every priority lane.
			queue_lane.clear();
		}
	}

private:
	/// @brief Sum of every lane's capacity: the size of the flat pool the lanes are carved from.
	static constexpr uint32_t total_capacity() {
		uint32_t total = 0;
		for (size_t i = 0; i < LaneCount; i++) {
			total += Capacities[i];
		}
		return total;
	}

	static_assert(([] {
		              for (size_t i = 0; i < LaneCount; i++) {
			              if (Capacities[i] == 0 || (Capacities[i] & (Capacities[i] - 1)) != 0) {
				              return false;
			              }
		              }
		              return true;
	              })(),
	              "every lane capacity must be an exact power of two: positions wrap with a bitmask");

	/// @brief One flat allocation the lanes carve slices from, so lanes can differ in capacity without
	///        becoming different types.
	std::array<T, total_capacity()> pool{};
};

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
	/// @brief Returns remaining USB send-queue capacity.
	/// @return Free queue space reported as MIDI payload bytes across all priority lanes, not as
	///         4-byte USB event slots.
	[[nodiscard]] int send_buffer_space() const;
	/// @brief Queues one packed USB-MIDI event, classifying it into the correct priority lane.
	/// @param full_message Packed USB-MIDI event.
	/// @param intent       Sender intent used to route Event vs. Continuous CCs into the correct lane.
	/// @return True when the queued backlog has grown past the flush threshold and the caller should
	///         flush. The queue manager deliberately does not flush itself: it is owned by the engine
	///         it would have to call, and a mainline enqueue must not trigger the interrupt-masked drain.
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
