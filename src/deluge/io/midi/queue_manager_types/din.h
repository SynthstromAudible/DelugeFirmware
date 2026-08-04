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

class MIDIQueueManagerDINPorts {
public:
	MIDIQueueManagerDINPorts() = default;

	/// Resets serial pacing/budget state to a known baseline at the provided sample timestamp.
	void reset_serial_state(uint32_t now_sample_timer);
	/// Returns true when any DIN priority lane has queued bytes waiting to be flushed.
	[[nodiscard]] bool has_serial_data() const;
	/// Classifies, optionally coalesces, and enqueues one outgoing MIDI message into DIN priority lanes.
	void enqueue_serial_message(MIDIMessage message);
	/// Drains queued DIN bytes into UART using pacing budget, lane priorities, and CC gating.
	void flush_serial_output(uint32_t now_sample_timer);

private:
	struct SerialByteQueue {
		static constexpr uint16_t k_capacity = 512;

		std::array<uint8_t, k_capacity> data{};
		uint16_t read_pos{0};
		uint16_t write_pos{0};

		[[nodiscard]] bool empty() const { return read_pos == write_pos; }
		[[nodiscard]] uint16_t size() const { return (write_pos - read_pos) & (k_capacity - 1); }
		[[nodiscard]] uint16_t space() const { return (k_capacity - 1) - size(); }
		[[nodiscard]] uint8_t peek(uint16_t offset = 0) const { return data[(read_pos + offset) & (k_capacity - 1)]; }
		/// Pushes one byte into the ring if space exists; returns false when full.
		bool push(uint8_t byte);
		/// Pops one byte from the ring head into out; returns false when empty.
		bool pop(uint8_t& out);
		/// Atomically pops count bytes into out; returns false unless the full span is available.
		bool pop_many(uint8_t* out, uint16_t count);
	};

	/// Number of active serial-priority lanes [clock..CC] scanned during dequeue.
	static constexpr size_t k_serial_priority_count = QUEUE_PRIORITY_CC + 1;
	/// Per-priority byte rings holding pending DIN output grouped by queue policy.
	std::array<SerialByteQueue, k_serial_priority_count> serial_priority_queues_{};
	/// Last sample-timer tick used to accrue DIN pacing budget.
	uint32_t serial_budget_last_update_{0};
	/// Token-bucket send budget in Q8 bytes (8 fractional bits).
	int32_t serial_budget_Q8_{0};
	/// Scratch buffer used when removing a queued CC frame and compacting survivors.
	std::array<uint8_t, SerialByteQueue::k_capacity> cc_reorder_scratch_{};
	/// Snapshot of first queued CC offset per controller for fair candidate selection.
	std::array<uint16_t, kMaxMIDIValue + 1> cc_fair_first_offsets_{};
	/// Round-robin controller cursor used as fairness baseline between dequeues.
	uint8_t cc_fair_next_controller_{0};
	/// Saturating per-controller enqueue pressure used by debt-aware fair dequeue.
	uint8_t cc_fair_controller_debt_[kMaxMIDIValue + 1]{};

	/// Refills Q8 pacing budget from elapsed sample time and applies idle-burst capping.
	void update_serial_budget(uint32_t now_sample_timer);
	/// Replaces the newest queued matching CC value instead of appending a duplicate write.
	bool coalesce_queued_cc(MIDIMessage message);
	/// Pops one queued 3-byte CC message selected by round-robin/debt fairness policy.
	bool pop_fair_queued_cc_message(uint8_t* out_bytes, int32_t budget_bytes, int32_t uart_space, int32_t max_len,
	                                QueuePriority& popped_priority);
	/// Removes one queued CC frame at target offset and compacts remaining queue bytes in-order.
	bool remove_queued_cc_message_at_offset(uint16_t target_offset, uint8_t* out_bytes);
	/// Enqueues one complete byte span atomically into the selected priority lane.
	bool enqueue_serial_bytes(QueuePriority priority, uint8_t const* bytes, int32_t len);
	/// Pops the next eligible realtime or full MIDI message under budget/space constraints.
	int32_t pop_next_prioritized_bytes(uint8_t* out_bytes, int32_t max_len, int32_t budget_bytes, int32_t uart_space,
	                                   int32_t cc_uart_budget, QueuePriority& popped_priority);
};

extern MIDIQueueManagerDINPorts midiQueueManagerDINPorts;
