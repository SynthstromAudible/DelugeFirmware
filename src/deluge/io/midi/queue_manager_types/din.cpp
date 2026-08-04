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

#include "io/midi/queue_manager_types/din.h"

#include "io/midi/midi_queue_manager.h"
#include "processing/engines/audio_engine.h"
#include <algorithm>

extern "C" {
#include "RZA1/uart/sio_char.h"
#include "drivers/uart/uart.h"
}

namespace {
// DIN link throughput in Q8 fixed-point bytes/second (31.25 kbps ~= 3125 bytes/s).
constexpr int32_t k_serial_bytes_per_second_Q8 = 3125 * 256;
// Maximum accumulated send budget (Q8 bytes) allowed for one burst after idle time.
constexpr int32_t k_serial_queue_budget_max_Q8 = MIDI_TX_BUFFER_SIZE * 256;
// Reserve some UART TX space so we do not fill the hardware buffer to the edge.
constexpr int32_t k_serial_uart_headroom_bytes = 16;
// Limit how much lowest-priority CC traffic can be staged ahead in the DIN UART buffer.
constexpr int32_t k_serial_buffered_cc_bytes_cap = 24;
} // namespace

MIDIQueueManagerDINPorts midiQueueManagerDINPorts{};

/// Resets serial pacing state so the next flush starts from a known baseline.
void MIDIQueueManagerDINPorts::reset_serial_state(uint32_t now_sample_timer) {
	// Start pacing from "now" and with zero carry-over send budget.
	serial_budget_last_update_ = now_sample_timer;
	serial_budget_Q8_ = 0;
}

/// Pushes one byte into a serial-priority ring buffer lane.
bool MIDIQueueManagerDINPorts::SerialByteQueue::push(uint8_t byte) {
	// Ring wraps with a mask because capacity is a power of two.
	uint16_t next = (write_pos + 1) & (k_capacity - 1);
	// Keep one slot open so full vs empty remains distinguishable.
	if (next == read_pos) {
		return false;
	}
	data[write_pos] = byte;
	write_pos = next;
	return true;
}

/// Pops one byte from a serial-priority ring buffer lane.
bool MIDIQueueManagerDINPorts::SerialByteQueue::pop(uint8_t& out) {
	// read_pos == write_pos means queue is empty.
	if (read_pos == write_pos) {
		return false;
	}
	out = data[read_pos];
	// Consume one byte and wrap cursor within ring capacity.
	read_pos = (read_pos + 1) & (k_capacity - 1);
	return true;
}

/// Pops `count` bytes atomically from a serial-priority ring buffer lane.
bool MIDIQueueManagerDINPorts::SerialByteQueue::pop_many(uint8_t* out, uint16_t count) {
	// All-or-nothing pop to preserve complete MIDI message boundaries.
	if (size() < count) {
		return false;
	}
	// Copy the logical span out of the ring, wrapping with the capacity mask.
	for (uint16_t i = 0; i < count; i++) {
		out[i] = data[(read_pos + i) & (k_capacity - 1)];
	}
	// Consume the copied span and wrap the cursor within ring capacity.
	read_pos = (read_pos + count) & (k_capacity - 1);
	return true;
}

/// Refills DIN serial pacing tokens from elapsed sample time.
void MIDIQueueManagerDINPorts::update_serial_budget(uint32_t now_sample_timer) {
	uint32_t delta_samples = now_sample_timer - serial_budget_last_update_;
	if (!delta_samples) {
		// No elapsed sample time means no new transmit budget to accrue.
		return;
	}
	serial_budget_last_update_ = now_sample_timer;

	// Q8 token bucket accumulation at classic DIN throughput.
	serial_budget_Q8_ +=
	    static_cast<int32_t>((static_cast<uint64_t>(delta_samples) * k_serial_bytes_per_second_Q8) / kSampleRate);
	if (serial_budget_Q8_ > k_serial_queue_budget_max_Q8) {
		// Cap idle-time bursts so one flush cannot monopolize UART.
		serial_budget_Q8_ = k_serial_queue_budget_max_Q8;
	}
}

/// Returns whether any serial-priority lane currently has data pending.
bool MIDIQueueManagerDINPorts::has_serial_data() const {
	// Fast pre-check before attempting a paced drain pass.
	for (auto const& queue : serial_priority_queues_) {
		if (!queue.empty()) {
			// One populated lane is enough to indicate pending serial output work.
			return true;
		}
	}
	return false;
}

/// Removes a 3-byte CC message from an arbitrary byte offset in the CC ring.
///
/// Fair dequeue may select a controller whose earliest queued message is not at
/// the ring head. To preserve message order and atomicity, this function copies
/// out the selected message, rebuilds the remaining bytes in-order, and resets
/// ring cursors to the rebuilt layout.
bool MIDIQueueManagerDINPorts::remove_queued_cc_message_at_offset(uint16_t target_offset, uint8_t* out_bytes) {
	SerialByteQueue& queue = serial_priority_queues_[QUEUE_PRIORITY_CC];
	uint16_t queue_size = queue.size();
	if (target_offset + 3 > queue_size) {
		// Selected CC span must be fully in-bounds of the current queue snapshot.
		return false;
	}

	// Fair selection can target a CC message in the middle of the byte ring,
	// so remove atomically by rebuilding the remaining bytes in-order.
	for (uint16_t i = 0; i < 3; i++) {
		out_bytes[i] = queue.peek(target_offset + i);
	}

	// Repack queue contents minus the selected 3-byte CC span.
	uint16_t scratch_size = 0;
	for (uint16_t i = 0; i < queue_size; i++) {
		if (i >= target_offset && i < target_offset + 3) {
			continue;
		}
		// Preserve byte order for all surviving queued messages.
		cc_reorder_scratch_[scratch_size++] = queue.peek(i);
	}

	// Reinitialize ring cursors, then replay compacted bytes as the new queue image.
	queue.read_pos = 0;
	queue.write_pos = 0;
	for (uint16_t i = 0; i < scratch_size; i++) {
		queue.push(cc_reorder_scratch_[i]);
	}

	return true;
}

/// Pops one queued CC message using controller-aware fairness.
///
/// Selection policy:
/// 1. Collect each controller's first queued CC offset.
/// 2. Start from `cc_fair_next_controller_` for round-robin ordering.
/// 3. Prefer highest controller debt; use RR order as tie-break.
/// 4. Remove the selected message atomically via offset-based removal.
///
/// Returns `false` when budgets/space do not allow a full 3-byte CC message,
/// when queue data is malformed, or when no eligible CC is present.
bool MIDIQueueManagerDINPorts::pop_fair_queued_cc_message(uint8_t* out_bytes, int32_t budget_bytes, int32_t uart_space,
                                                          int32_t max_len, QueuePriority& popped_priority) {
	// Fair CC dequeue is atomic on one full 3-byte message; bail out if any limiter
	// (DIN budget, UART space, or output span) cannot accommodate that minimum.
	if (budget_bytes < 3 || uart_space < 3 || max_len < 3) {
		return false;
	}

	SerialByteQueue& queue = serial_priority_queues_[QUEUE_PRIORITY_CC];
	uint16_t queue_size = queue.size();
	if (queue_size < 3) {
		// No possible CC candidate if fewer than 3 bytes are queued.
		return false;
	}

	// Initialize this scan snapshot to "no queued packet for this controller".
	auto& first_offsets = MIDIQueueManager::initialize_first_controller_offsets(cc_fair_first_offsets_);
	// Tracks whether this queue snapshot contains any channel-CC packets at all.
	bool saw_any_cc = false;

	// Walk message-by-message so offsets always align to parsed MIDI frames, then
	// capture the first queued CC packet per controller for fair candidate selection.
	for (uint16_t offset = 0; offset < queue_size;) {
		uint8_t status = queue.peek(offset);
		int32_t message_len = bytesPerStatusMessage(status);
		if (message_len <= 0 || offset + message_len > queue_size) {
			// Abort instead of consuming bytes from malformed/partial message boundaries.
			return false;
		}

		// Consider only canonical 3-byte channel-CC frames when building per-
		// controller dequeue candidates for fairness selection.
		if (message_len == 3 && (status >> 4) == 0x0B) {
			saw_any_cc = true;
			uint8_t controller = queue.peek(offset + 1);
			// Capture only the first offset per controller; later occurrences remain behind it in-order.
			MIDIQueueManager::record_first_controller_offset(first_offsets, controller, offset);
		}

		// Step by one parsed MIDI frame to keep subsequent reads aligned.
		offset += static_cast<uint16_t>(message_len);
	}

	if (!saw_any_cc) {
		// No eligible CC frames were discovered during the scan, so fairness
		// selection has no candidates to dequeue this pass.
		return false;
	}

	// Candidate selection uses shared RR+debt policy logic.
	uint16_t selected_offset = 0;
	uint8_t selected_controller = 0;
	if (!MIDIQueueManager::select_fair_controller_candidate(
	        first_offsets, cc_fair_next_controller_, cc_fair_controller_debt_, selected_offset, selected_controller)) {
		// Sweep found no eligible controller candidate to dequeue this pass.
		return false;
	}

	// Commit nothing unless the selected CC can be removed atomically.
	if (!remove_queued_cc_message_at_offset(selected_offset, out_bytes)) {
		return false;
	}

	// Successful dequeue commits fairness bookkeeping and rotates RR start cursor.
	MIDIQueueManager::commit_fair_controller_service(cc_fair_controller_debt_, cc_fair_next_controller_,
	                                                 selected_controller);
	popped_priority = QUEUE_PRIORITY_CC;
	return true;
}

/// Coalesces a queued CC by replacing the newest matching pending value.
bool MIDIQueueManagerDINPorts::coalesce_queued_cc(MIDIMessage message) {
	if (message.statusType != 0x0B) {
		// Only channel CC messages are eligible for in-queue value replacement.
		return false;
	}

	SerialByteQueue& queue = serial_priority_queues_[QUEUE_PRIORITY_CC];
	uint16_t queue_size = queue.size();
	if (queue_size < 3) {
		// A complete CC frame is 3 bytes, so shorter queues cannot contain one.
		return false;
	}

	// Match only the same channel+status byte; sentinel -1 means no queued
	// packet for this controller/channel pair has been found yet.
	uint8_t wanted_status = message.channel | (message.statusType << 4);
	int32_t latest_match_offset = -1;

	for (uint16_t offset = 0; offset < queue_size;) {
		uint8_t status = queue.peek(offset);
		int32_t message_len = bytesPerStatusMessage(status);
		if (message_len <= 0 || offset + message_len > queue_size) {
			// Stop coalescing scan on malformed boundary rather than mutating unknown bytes.
			break;
		}

		// Match only full 3-byte CC packets for the same status/channel and
		// controller number; those are eligible for in-place value replacement.
		if (message_len == 3 && status == wanted_status && queue.peek(offset + 1) == message.data1) {
			// Keep walking to coalesce the newest pending value for this controller.
			latest_match_offset = offset;
		}

		offset += message_len;
	}

	if (latest_match_offset < 0) {
		// No pending packet for this controller/channel pair; caller may enqueue normally.
		return false;
	}

	// Patch only the value byte of the newest matching queued packet.
	uint16_t value_index = (queue.read_pos + latest_match_offset + 2) & (SerialByteQueue::k_capacity - 1);
	queue.data[value_index] = message.data2;
	// Treat coalesce as fresh pressure so fairness can compensate relative enqueue rate.
	MIDIQueueManager::bump_controller_debt(cc_fair_controller_debt_, message.data1);
	return true;
}

/// Enqueues a complete byte sequence into one serial-priority lane.
bool MIDIQueueManagerDINPorts::enqueue_serial_bytes(QueuePriority priority, uint8_t const* bytes, int32_t len) {
	SerialByteQueue& queue = serial_priority_queues_[static_cast<uint8_t>(priority)];
	if (len <= 0) {
		// Empty payload is a successful no-op; callers can treat it as enqueued.
		return true;
	}
	if (queue.space() < len) {
		// Reject atomically if lane is full; never enqueue partial messages.
		return false;
	}
	// Space is guaranteed above, so this loop commits the whole message payload.
	for (int32_t i = 0; i < len; i++) {
		queue.push(bytes[i]);
	}
	return true;
}

/// Pops one realtime byte or one full MIDI message under budget and UART-space limits.
int32_t MIDIQueueManagerDINPorts::pop_next_prioritized_bytes(uint8_t* out_bytes, int32_t max_len, int32_t budget_bytes,
                                                             int32_t uart_space, int32_t cc_uart_budget,
                                                             QueuePriority& popped_priority) {
	// Nothing can be emitted if caller buffer room, DIN token budget, or UART
	// space is already exhausted for this scheduling pass.
	if (max_len <= 0 || budget_bytes <= 0 || uart_space <= 0) {
		return 0;
	}

	constexpr size_t k_clock_idx = QUEUE_PRIORITY_CLOCK;
	constexpr size_t k_cc_idx = QUEUE_PRIORITY_CC;

	// Scan lanes in strict priority order (clock -> notes -> expression -> CC)
	// and stop at the first lane that can produce a full eligible payload.
	for (size_t idx = k_clock_idx; idx <= k_cc_idx; idx++) {
		SerialByteQueue& queue = serial_priority_queues_[idx];
		if (queue.empty()) {
			// This lane has no work; advance to the next priority lane in-order.
			continue;
		}

		if (idx == k_clock_idx) {
			// Realtime/clock lane is byte-oriented: emit at most one byte per call.
			if (budget_bytes < 1 || uart_space < 1 || max_len < 1) {
				return 0;
			}
			queue.pop(out_bytes[0]);
			// Report the lane and exact byte count so caller accounting stays correct.
			popped_priority = static_cast<QueuePriority>(idx);
			return 1;
		}

		if (idx == k_cc_idx) {
			// Parse the queue-head message once so we can gate CC dequeue safely.
			uint8_t status = queue.peek();
			int32_t message_len = bytesPerStatusMessage(status);
			if (message_len <= 0) {
				// Unknown head status: defer and try again on a later scheduler pass.
				return 0;
			}

			// Never dequeue a CC message if the staged-CC UART budget is exhausted.
			if ((status >> 4) == 0x0B && message_len == 3 && cc_uart_budget < 3) {
				return 0;
			}

			// The CC-priority lane may contain non-CC channel messages (e.g. program change).
			// Only run fair dequeue for actual 3-byte CC messages.
			if ((status >> 4) == 0x0B && message_len == 3) {
				if (pop_fair_queued_cc_message(out_bytes, budget_bytes, uart_space, max_len, popped_priority)) {
					return 3;
				}
			}
		}

		// For channel messages, prefer popping whole messages to avoid fragmentation.
		uint8_t status = queue.peek();
		int32_t message_len = bytesPerStatusMessage(status);
		// Unknown status (or malformed queue content) is skipped safely this pass.
		if (message_len <= 0) {
			return 0;
		}
		// Require full message fit in queue/budget/space for atomic send.
		if (queue.size() < message_len || budget_bytes < message_len || uart_space < message_len
		    || max_len < message_len) {
			return 0;
		}
		// Keep this final guard even after fit checks: pop_many is the atomic boundary.
		if (!queue.pop_many(out_bytes, message_len)) {
			return 0;
		}
		// Report both the source lane and actual byte count so caller accounting stays correct.
		popped_priority = static_cast<QueuePriority>(idx);
		return message_len;
	}

	return 0;
}

/// Encodes and enqueues one channel/system MIDI message into serial-priority lanes.
void MIDIQueueManagerDINPorts::enqueue_serial_message(MIDIMessage message) {
	// Convert message to wire bytes and queue by shared priority policy.
	uint8_t status_byte = message.channel | (message.statusType << 4);
	uint8_t raw_bytes[3] = {status_byte, message.data1, message.data2};
	int32_t message_length = bytesPerStatusMessage(status_byte);
	QueuePriority priority = MIDIQueueManager::classify_message(message);
	if (priority == QUEUE_PRIORITY_CC && coalesce_queued_cc(message)) {
		// For dense CC streams, replace pending controller value instead of appending another packet.
		return;
	}

	// Atomic lane enqueue; false means lane full and message is intentionally dropped.
	bool queued_ok = enqueue_serial_bytes(priority, raw_bytes, message_length);
	if (priority == QUEUE_PRIORITY_CC) {
		if (!queued_ok) {
			// Do not update fairness state for data that never entered the queue.
			return;
		}
		// Debt tracks relative enqueue/coalesce pressure so dequeue can compensate fairly.
		uint8_t controller = message.data1;
		if (controller <= kMaxMIDIValue) {
			// Mark this controller as newly backlogged so fair dequeue can compensate.
			MIDIQueueManager::bump_controller_debt(cc_fair_controller_debt_, controller);
		}
	}
}

/// Drains serial-priority queues into UART while enforcing DIN pacing and strict priority gates.
void MIDIQueueManagerDINPorts::flush_serial_output(uint32_t now_sample_timer) {
	if (!has_serial_data()) {
		// Fast exit when all lanes are empty; avoids pacing/space calculations.
		return;
	}

	// Apply DIN pacing before deciding this iteration's send allowance.
	update_serial_budget(now_sample_timer);

	int32_t raw_uart_space = uartGetTxBufferSpace(UART_ITEM_MIDI);
	int32_t uart_space = raw_uart_space - k_serial_uart_headroom_bytes;
	if (uart_space <= 0) {
		// Preserve a little headroom so other UART activity is not starved.
		return;
	}
	// This counts CC bytes already staged in the UART path and limits additional
	// queued CC so later clock/notes can still preempt promptly.
	int32_t cc_uart_budget =
	    std::max<int32_t>(0, k_serial_buffered_cc_bytes_cap - (MIDI_TX_BUFFER_SIZE - raw_uart_space));

	// Convert Q8 token budget to whole bytes available for this drain pass.
	int32_t send_allowance_bytes = serial_budget_Q8_ >> 8;
	constexpr size_t k_clock_idx = QUEUE_PRIORITY_CLOCK;
	// If no budget exists and no realtime clock is waiting, defer this pass.
	if (send_allowance_bytes <= 0 && serial_priority_queues_[k_clock_idx].empty()) {
		return;
	}

	if (send_allowance_bytes <= 0) {
		// Allow one realtime byte to pass when budget is depleted.
		send_allowance_bytes = 1;
	}

	int32_t sent = 0;
	// Keep draining while both UART capacity and token budget remain.
	while (uart_space > 0 && send_allowance_bytes > 0) {
		uint8_t bytes_to_send[3] = {0, 0, 0};
		QueuePriority popped_priority = QUEUE_PRIORITY_CC;
		int32_t bytes_popped = pop_next_prioritized_bytes(bytes_to_send, 3, send_allowance_bytes, uart_space,
		                                                  cc_uart_budget, popped_priority);
		if (bytes_popped <= 0) {
			break;
		}

		for (int32_t i = 0; i < bytes_popped; i++) {
			// Push selected bytes into the UART MIDI TX buffer.
			bufferMIDIUart(bytes_to_send[i]);
		}
		sent += bytes_popped;

		bool is_cc_message = (popped_priority == QUEUE_PRIORITY_CC);
		if (is_cc_message) {
			// Decrement staged-CC budget only for actual CC-lane output.
			cc_uart_budget -= bytes_popped;
		}
		// Only commit fairness state when a full 3-byte channel-CC frame with a
		// valid controller number has actually been emitted to UART.
		if (is_cc_message && bytes_popped == 3 && (bytes_to_send[0] >> 4) == 0x0B
		    && bytes_to_send[1] <= kMaxMIDIValue) {
			uint8_t dequeued_controller = bytes_to_send[1];
			// Successful transmit repays that controller's pressure.
			cc_fair_controller_debt_[dequeued_controller] = 0;
		}
		uart_space -= bytes_popped;
		send_allowance_bytes -= bytes_popped;
	}

	if (sent > 0) {
		// Convert whole bytes back to Q8 units and debit pacing bucket.
		serial_budget_Q8_ = std::max<int32_t>(0, serial_budget_Q8_ - sent * 256);
	}
}
