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

	void reset_serial_state(uint32_t now_sample_timer);
	[[nodiscard]] bool has_serial_data() const;
	void enqueue_serial_message(MIDIMessage message);
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
		bool push(uint8_t byte);
		bool pop(uint8_t& out);
		bool pop_many(uint8_t* out, uint16_t count);
	};

	static constexpr size_t k_serial_priority_count = QUEUE_PRIORITY_CC + 1;
	std::array<SerialByteQueue, k_serial_priority_count> serial_priority_queues_{};
	uint32_t serial_budget_last_update_{0};
	int32_t serial_budget_Q8_{0};
	std::array<uint8_t, SerialByteQueue::k_capacity> cc_reorder_scratch_{};
	std::array<uint16_t, 128> cc_fair_first_offsets_{};
	uint8_t cc_fair_next_controller_{0};
	uint8_t cc_fair_controller_debt_[128]{};

	void update_serial_budget(uint32_t now_sample_timer);
	bool coalesce_queued_cc(MIDIMessage message);
	bool pop_fair_queued_cc_message(uint8_t* out_bytes, int32_t budget_bytes, int32_t uart_space, int32_t max_len,
	                                QueuePriority& popped_priority);
	bool remove_queued_cc_message_at_offset(uint16_t target_offset, uint8_t* out_bytes);
	bool enqueue_serial_bytes(QueuePriority priority, uint8_t const* bytes, int32_t len);
	int32_t pop_next_prioritized_bytes(uint8_t* out_bytes, int32_t max_len, int32_t budget_bytes, int32_t uart_space,
	                                   int32_t cc_uart_budget, QueuePriority& popped_priority);
};

extern MIDIQueueManagerDINPorts midiQueueManagerDINPorts;
