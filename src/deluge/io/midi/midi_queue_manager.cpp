/*
 * Copyright © 2026 Synthstrom Audible Limited
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
#include "io/midi/midi_device_manager.h"
#include "processing/engines/audio_engine.h"
#include <algorithm>

extern "C" {
#include "RZA1/uart/sio_char.h"
#include "drivers/uart/uart.h"
}

namespace {
// DIN link throughput in Q8 fixed-point bytes/second (31.25 kbps ~= 3125 bytes/s).
constexpr int32_t kMidiDinBytesPerSecondQ8 = 3125 * 256;
// Maximum accumulated send budget (Q8 bytes) allowed for one burst after idle time.
constexpr int32_t kSerialQueueBudgetMaxQ8 = MIDI_TX_BUFFER_SIZE * 256;
// Reserve some UART TX space so we do not fill the hardware buffer to the edge.
constexpr int32_t kSerialUartHeadroomBytes = 16;
// Limit how much lowest-priority CC traffic can be staged ahead in the DIN UART buffer.
// This keeps dense CC bursts from sitting on the wire ahead of later clock or note bytes
// even when the software queues themselves are draining cleanly.
constexpr int32_t kSerialBufferedCCBytesCap = 24;
} // namespace

MidiQueueManager midiQueueManager{};

MidiQueueManager::MidiQueueManager() = default;

/// Classifies an outgoing MIDI message into shared queue priorities.
QueuePriority MidiQueueManager::classifyMessage(MIDIMessage message) {
	if (message.isSystemMessage()) {
		// Keep transport / realtime bytes at the highest priority lane.
		return QUEUE_PRIORITY_CLOCK;
	}

	switch (message.statusType) {
	case 0x08:
	case 0x09:
		// 0x08 = Note Off, 0x09 = Note On.
		return QUEUE_PRIORITY_NOTES;

	case 0x0A:
	case 0x0D:
	case 0x0E:
		// Poly AT, Channel AT, and Pitch Bend are expression-style controls.
		return QUEUE_PRIORITY_EXPRESSION;

	case 0x0B:
		// Treat modulation (CC1) and brightness/timbre (CC74) like expression for MPE playability.
		if (message.data1 == 1 || message.data1 == 74) {
			return QUEUE_PRIORITY_EXPRESSION;
		}
		return QUEUE_PRIORITY_CC;

	default:
		return QUEUE_PRIORITY_CC;
	}
}

/// Returns queued USB packet count for one lane via monotonic write/read counters.
uint16_t MidiQueueManager::usbQueueCount(ConnectedUSBMIDIDevice const* device, QueuePriority priority) {
	// Monotonic write/read counters: occupancy is their difference for each lane.
	uint8_t p = static_cast<uint8_t>(priority);
	return static_cast<uint16_t>(device->ringBufWriteIdx[p] - device->ringBufReadIdx[p]);
}

/// Returns total queued USB packet count across all priority lanes.
uint32_t MidiQueueManager::usbTotalQueuedMessages(ConnectedUSBMIDIDevice const* device) {
	// Aggregate backlog across all USB priority lanes.
	uint32_t queued = 0;
	for (uint8_t p = 0; p < QUEUE_PRIORITY_COUNT; p++) {
		queued += usbQueueCount(device, static_cast<QueuePriority>(p));
	}
	return queued;
}

/// Pops one USB packet using strict priority ordering: clock > notes > expression > CC > SysEx.
bool MidiQueueManager::usbPopPriorityMessage(ConnectedUSBMIDIDevice* device, uint32_t& messageOut) {
	for (uint8_t p = QUEUE_PRIORITY_CLOCK; p < QUEUE_PRIORITY_COUNT; p++) {
		QueuePriority priority = static_cast<QueuePriority>(p);
		if (!usbQueueCount(device, priority)) {
			continue;
		}

		// Power-of-two mask wraps index without modulo cost.
		messageOut = device->sendDataRingBuf[p][device->ringBufReadIdx[p] & MIDI_SEND_RING_MASK];
		device->ringBufReadIdx[p]++;
		return true;
	}

	return false;
}

/// Pushes one USB packet onto a selected priority lane.
void MidiQueueManager::usbPushPriorityMessage(ConnectedUSBMIDIDevice* device, QueuePriority priority,
                                              uint32_t message) {
	// Power-of-two mask wraps index without modulo cost.
	uint8_t p = static_cast<uint8_t>(priority);
	device->sendDataRingBuf[p][device->ringBufWriteIdx[p] & MIDI_SEND_RING_MASK] = message;
	device->ringBufWriteIdx[p]++;
}

/// Resets all USB per-priority queues and read/write cursors.
void MidiQueueManager::resetUsbQueueStorage(ConnectedUSBMIDIDevice* device) {
	// storage cleared for deterministic startup, and read/write cursors reset to zero.
	for (auto& queue_lane : device->sendDataRingBuf) {
		queue_lane.fill(0);
	}
	device->ringBufWriteIdx.fill(0);
	device->ringBufReadIdx.fill(0);
}

/// Resets serial pacing state so the next flush starts from a known baseline.
void MidiQueueManager::resetSerialState(uint32_t nowSampleTimer) {
	// Start pacing from "now" and with zero carry-over send budget.
	serialBudgetLastUpdate_ = nowSampleTimer;
	serialDinBudgetQ8_ = 0;
}

/// Pushes one byte into a serial-priority ring buffer lane.
bool MidiQueueManager::SerialByteQueue::push(uint8_t byte) {
	// Ring wraps with a mask because capacity is a power of two.
	uint16_t next = (writePos + 1) & (kCapacity - 1);
	// Keep one slot open so full vs empty remains distinguishable.
	if (next == readPos) {
		return false;
	}
	data[writePos] = byte;
	writePos = next;
	return true;
}

/// Pops one byte from a serial-priority ring buffer lane.
bool MidiQueueManager::SerialByteQueue::pop(uint8_t& out) {
	// readPos == writePos means queue is empty.
	if (readPos == writePos) {
		return false;
	}
	out = data[readPos];
	readPos = (readPos + 1) & (kCapacity - 1);
	return true;
}

/// Pops `count` bytes atomically from a serial-priority ring buffer lane.
bool MidiQueueManager::SerialByteQueue::popMany(uint8_t* out, uint16_t count) {
	// All-or-nothing pop to preserve complete MIDI message boundaries.
	if (size() < count) {
		return false;
	}
	for (uint16_t i = 0; i < count; i++) {
		out[i] = data[(readPos + i) & (kCapacity - 1)];
	}
	readPos = (readPos + count) & (kCapacity - 1);
	return true;
}

/// Refills DIN serial pacing tokens from elapsed sample time.
void MidiQueueManager::updateSerialDinBudget(uint32_t nowSampleTimer) {
	uint32_t deltaSamples = nowSampleTimer - serialBudgetLastUpdate_;
	if (!deltaSamples) {
		return;
	}
	serialBudgetLastUpdate_ = nowSampleTimer;

	// Q8 token bucket accumulation at classic DIN throughput.
	serialDinBudgetQ8_ +=
	    static_cast<int32_t>((static_cast<uint64_t>(deltaSamples) * kMidiDinBytesPerSecondQ8) / kSampleRate);
	if (serialDinBudgetQ8_ > kSerialQueueBudgetMaxQ8) {
		// Cap idle-time bursts so one flush cannot monopolize UART.
		serialDinBudgetQ8_ = kSerialQueueBudgetMaxQ8;
	}
}

/// Returns whether any serial-priority lane currently has data pending.
bool MidiQueueManager::hasSerialData() const {
	// Fast pre-check before attempting a paced drain pass.
	for (auto const& queue : serialPriorityQueues_) {
		if (!queue.empty()) {
			return true;
		}
	}
	return false;
}

/// Enqueues a complete byte sequence into one serial-priority lane.
bool MidiQueueManager::enqueueSerialBytes(QueuePriority priority, uint8_t const* bytes, int32_t len) {
	SerialByteQueue& queue = serialPriorityQueues_[static_cast<uint8_t>(priority)];
	if (len <= 0) {
		return true;
	}
	if (queue.space() < len) {
		// Reject atomically if lane is full; never enqueue partial messages.
		return false;
	}
	for (int32_t i = 0; i < len; i++) {
		queue.push(bytes[i]);
	}
	return true;
}

/// Pops one realtime byte or one full MIDI message under budget and UART-space limits.
int32_t MidiQueueManager::popNextPrioritizedBytes(uint8_t* outBytes, int32_t maxLen, int32_t budgetBytes,
                                                  int32_t uartSpace, QueuePriority& poppedPriority) {
	if (maxLen <= 0 || budgetBytes <= 0 || uartSpace <= 0) {
		return 0;
	}

	constexpr size_t kClockIdx = QUEUE_PRIORITY_CLOCK;
	constexpr size_t kCcIdx = QUEUE_PRIORITY_CC;

	for (size_t idx = kClockIdx; idx <= kCcIdx; idx++) {
		SerialByteQueue& queue = serialPriorityQueues_[idx];
		if (queue.empty()) {
			continue;
		}

		if (idx == kClockIdx) {
			if (budgetBytes < 1 || uartSpace < 1 || maxLen < 1) {
				return 0;
			}
			queue.pop(outBytes[0]);
			poppedPriority = static_cast<QueuePriority>(idx);
			return 1;
		}

		// For channel messages, prefer popping whole messages to avoid fragmentation.
		uint8_t status = queue.peek();
		int32_t messageLen = bytesPerStatusMessage(status);
		// Unknown status (or malformed queue content) is skipped safely this pass.
		if (messageLen <= 0) {
			return 0;
		}
		// Require full message fit in queue/budget/space for atomic send.
		if (queue.size() < messageLen || budgetBytes < messageLen || uartSpace < messageLen || maxLen < messageLen) {
			return 0;
		}
		if (!queue.popMany(outBytes, messageLen)) {
			return 0;
		}
		poppedPriority = static_cast<QueuePriority>(idx);
		return messageLen;
	}

	return 0;
}

/// Encodes and enqueues one channel/system MIDI message into serial-priority lanes.
void MidiQueueManager::enqueueSerialMidiMessage(MIDIMessage message) {
	// Convert message to wire bytes and queue by shared priority policy.
	uint8_t statusByte = message.channel | (message.statusType << 4);
	uint8_t rawBytes[3] = {statusByte, message.data1, message.data2};
	int32_t messageLength = bytesPerStatusMessage(statusByte);
	enqueueSerialBytes(classifyMessage(message), rawBytes, messageLength);
}

/// Drains serial-priority queues into UART while enforcing DIN pacing and strict priority gates.
void MidiQueueManager::flushSerialOutput(uint32_t nowSampleTimer) {
	if (!hasSerialData()) {
		// Fast exit when all lanes are empty; avoids pacing/space calculations.
		return;
	}

	// Apply DIN pacing before deciding this iteration's send allowance.
	updateSerialDinBudget(nowSampleTimer);

	// Track total free MIDI UART capacity separately from the usable space for this flush.
	// The raw value is also used below to estimate how many non-clock bytes are already
	// staged in hardware/software UART buffering.
	int32_t rawUartSpace = uartGetTxBufferSpace(UART_ITEM_MIDI);
	int32_t uartSpace = rawUartSpace - kSerialUartHeadroomBytes;
	if (uartSpace <= 0) {
		// Preserve a little headroom so other UART activity is not starved.
		return;
	}
	// Bound outstanding CC bytes already staged in the UART buffer rather than only
	// limiting one flush pass. This directly constrains added wire-time latency for
	// later clock and note messages that arrive after earlier CC-heavy flushes.
	int32_t ccUartBudget = std::max<int32_t>(0, kSerialBufferedCCBytesCap - (MIDI_TX_BUFFER_SIZE - rawUartSpace));

	// Convert Q8 token budget to whole bytes available for this drain pass.
	int32_t sendAllowanceBytes = serialDinBudgetQ8_ >> 8;
	constexpr size_t kClockIdx = QUEUE_PRIORITY_CLOCK;
	// If no budget exists and no realtime clock is waiting, defer this pass.
	if (sendAllowanceBytes <= 0 && serialPriorityQueues_[kClockIdx].empty()) {
		return;
	}

	if (sendAllowanceBytes <= 0) {
		// Allow one realtime byte to pass when budget is depleted.
		sendAllowanceBytes = 1;
	}

	int32_t sent = 0;
	// Keep draining while both UART capacity and token budget remain.
	while (uartSpace > 0 && sendAllowanceBytes > 0) {
		uint8_t bytesToSend[3] = {0, 0, 0};
		QueuePriority poppedPriority = QUEUE_PRIORITY_CC;
		int32_t bytesPopped = popNextPrioritizedBytes(bytesToSend, 3, sendAllowanceBytes, uartSpace, poppedPriority);
		if (bytesPopped <= 0) {
			break;
		}

		bool isCcMessage = (poppedPriority == QUEUE_PRIORITY_CC);
		if (isCcMessage && ccUartBudget < bytesPopped) {
			// Yield until the hardware drains enough queued CC traffic; clock, note, and
			// expression messages remain eligible so higher-priority output can still preempt
			// dense automation at the next flush.
			break;
		}

		for (int32_t i = 0; i < bytesPopped; i++) {
			// Push selected bytes into the UART MIDI TX buffer.
			bufferMIDIUart(bytesToSend[i]);
		}
		sent += bytesPopped;
		if (isCcMessage) {
			// Only lowest-priority CC traffic consumes this staging cap. Higher-priority lanes
			// still use queue ordering plus available UART space, but are not blocked by CC-only
			// occupancy accounting.
			ccUartBudget -= bytesPopped;
		}
		uartSpace -= bytesPopped;
		sendAllowanceBytes -= bytesPopped;
	}

	if (sent > 0) {
		// Convert whole bytes back to Q8 units and debit pacing bucket.
		serialDinBudgetQ8_ = std::max<int32_t>(0, serialDinBudgetQ8_ - sent * 256);
	}
}
