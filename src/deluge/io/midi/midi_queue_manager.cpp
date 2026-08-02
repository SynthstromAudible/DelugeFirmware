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
#include "processing/engines/audio_engine.h"
#include <algorithm>
#include <cstring>

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
uint16_t MidiQueueManager::usbQueueCount(USBMidiSendQueueStorage const* storage, QueuePriority priority) {
	// Monotonic write/read counters: occupancy is their difference for each lane.
	uint8_t p = static_cast<uint8_t>(priority);
	return static_cast<uint16_t>(storage->ringBufWriteIdx[p] - storage->ringBufReadIdx[p]);
}

/// Returns total queued USB packet count across all priority lanes.
uint32_t MidiQueueManager::usbTotalQueuedMessages(USBMidiSendQueueStorage const* storage) {
	// Aggregate backlog across all USB priority lanes.
	uint32_t queued = 0;
	for (uint8_t p = 0; p < QUEUE_PRIORITY_COUNT; p++) {
		queued += usbQueueCount(storage, static_cast<QueuePriority>(p));
	}
	return queued;
}

/// Returns true if any USB lane above `priority` currently has queued packets.
bool MidiQueueManager::usbAnyHigherPriorityHasData(USBMidiSendQueueStorage const* storage, QueuePriority priority) {
	// Lower numeric value means higher queue priority.
	for (uint8_t p = 0; p < static_cast<uint8_t>(priority); p++) {
		if (usbQueueCount(storage, static_cast<QueuePriority>(p))) {
			return true;
		}
	}
	return false;
}

/// Pops one USB packet using strict priority ordering: clock > notes > expression > CC > SysEx.
bool MidiQueueManager::usbPopPriorityMessage(USBMidiSendQueueStorage* storage, uint32_t& messageOut) {
	// Strict ordering: clock/realtime first, then notes, then expression.
	for (uint8_t p = QUEUE_PRIORITY_CLOCK; p <= QUEUE_PRIORITY_EXPRESSION; p++) {
		QueuePriority priority = static_cast<QueuePriority>(p);
		if (usbQueueCount(storage, priority)) {
			// Power-of-two mask wraps index without modulo cost.
			messageOut = storage->sendDataRingBuf[p][storage->ringBufReadIdx[p] & MIDI_SEND_RING_MASK];
			storage->ringBufReadIdx[p]++;
			return true;
		}
	}

	// CC is allowed only when all higher lanes are empty.
	if (!usbAnyHigherPriorityHasData(storage, QUEUE_PRIORITY_CC) && usbQueueCount(storage, QUEUE_PRIORITY_CC)) {
		messageOut =
		    storage
		        ->sendDataRingBuf[QUEUE_PRIORITY_CC][storage->ringBufReadIdx[QUEUE_PRIORITY_CC] & MIDI_SEND_RING_MASK];
		storage->ringBufReadIdx[QUEUE_PRIORITY_CC]++;
		return true;
	}

	// SysEx is always last and only dequeued when all higher lanes are clear.
	if (!usbAnyHigherPriorityHasData(storage, QUEUE_PRIORITY_SYSEX) && usbQueueCount(storage, QUEUE_PRIORITY_SYSEX)) {
		messageOut = storage->sendDataRingBuf[QUEUE_PRIORITY_SYSEX]
		                                     [storage->ringBufReadIdx[QUEUE_PRIORITY_SYSEX] & MIDI_SEND_RING_MASK];
		storage->ringBufReadIdx[QUEUE_PRIORITY_SYSEX]++;
		return true;
	}

	return false;
}

/// Pushes one USB packet onto a selected priority lane.
void MidiQueueManager::usbPushPriorityMessage(USBMidiSendQueueStorage* storage, QueuePriority priority,
                                              uint32_t message) {
	// Power-of-two mask wraps index without modulo cost.
	uint8_t p = static_cast<uint8_t>(priority);
	storage->sendDataRingBuf[p][storage->ringBufWriteIdx[p] & MIDI_SEND_RING_MASK] = message;
	storage->ringBufWriteIdx[p]++;
}

/// Resets all USB per-priority queues and read/write cursors.
void MidiQueueManager::resetUsbQueueStorage(USBMidiSendQueueStorage* storage) {
	// storage cleared for deterministic startup, and read/write cursors reset to zero.
	memset(storage->sendDataRingBuf, 0, sizeof(storage->sendDataRingBuf));
	memset(storage->ringBufWriteIdx, 0, sizeof(storage->ringBufWriteIdx));
	memset(storage->ringBufReadIdx, 0, sizeof(storage->ringBufReadIdx));
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

/// Returns true if any serial-priority lane above `priority` still has data queued.
bool MidiQueueManager::hasHigherPriorityDataThan(QueuePriority priority) const {
	// Lower enum values represent higher priorities.
	for (size_t i = 0; i < static_cast<uint8_t>(priority); i++) {
		if (!serialPriorityQueues_[i].empty()) {
			return true;
		}
	}
	return false;
}

/// Pops one realtime byte or one full MIDI message under budget and UART-space limits.
int32_t MidiQueueManager::popNextPrioritizedBytes(uint8_t* outBytes, int32_t maxLen, int32_t budgetBytes,
                                                  int32_t uartSpace) {
	if (maxLen <= 0 || budgetBytes <= 0 || uartSpace <= 0) {
		return 0;
	}

	constexpr size_t kClockIdx = QUEUE_PRIORITY_CLOCK;
	constexpr size_t kNotesIdx = QUEUE_PRIORITY_NOTES;
	constexpr size_t kExpressionIdx = QUEUE_PRIORITY_EXPRESSION;
	constexpr size_t kCcIdx = QUEUE_PRIORITY_CC;
	constexpr size_t kSysexIdx = QUEUE_PRIORITY_SYSEX;

	// Realtime lane can preempt and send a single byte immediately.
	if (!serialPriorityQueues_[kClockIdx].empty()) {
		if (budgetBytes < 1 || uartSpace < 1 || maxLen < 1) {
			return 0;
		}
		serialPriorityQueues_[kClockIdx].pop(outBytes[0]);
		return 1;
	}

	// For channel messages, prefer popping whole messages to avoid fragmentation.
	auto tryPopWholeMessage = [&](size_t queueIdx) -> int32_t {
		SerialByteQueue& queue = serialPriorityQueues_[queueIdx];
		if (queue.empty()) {
			return 0;
		}

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
		return messageLen;
	};

	int32_t sent = tryPopWholeMessage(kNotesIdx);
	// After clock/realtime, note events are the next highest priority.
	if (sent > 0) {
		return sent;
	}

	sent = tryPopWholeMessage(kExpressionIdx);
	// Expression lanes run after notes so gestures stay responsive.
	if (sent > 0) {
		return sent;
	}

	// CC can flow only when no higher-priority lane currently has backlog.
	if (!hasHigherPriorityDataThan(QUEUE_PRIORITY_CC)) {
		sent = tryPopWholeMessage(kCcIdx);
		if (sent > 0) {
			return sent;
		}
	}

	// SysEx is strict lowest priority and only drains when all other lanes are clear.
	if (!hasHigherPriorityDataThan(QUEUE_PRIORITY_SYSEX)) {
		SerialByteQueue& sysexQueue = serialPriorityQueues_[kSysexIdx];
		if (sysexQueue.empty()) {
			return 0;
		}

		// SysEx can be long; stream it in bounded chunks between higher-priority opportunities.
		int32_t sysexBytes = std::min({maxLen, budgetBytes, uartSpace, static_cast<int32_t>(sysexQueue.size())});
		for (int32_t i = 0; i < sysexBytes; i++) {
			sysexQueue.pop(outBytes[i]);
		}
		return sysexBytes;
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

/// Enqueues a validated SysEx byte sequence into the serial SysEx priority lane.
void MidiQueueManager::enqueueSerialSysex(uint8_t const* data, int32_t len) {
	if (len < 3 || data[0] != 0xF0 || data[len - 1] != 0xF7) {
		// Ignore malformed SysEx so queue state remains sane.
		return;
	}

	enqueueSerialBytes(QUEUE_PRIORITY_SYSEX, data, len);
}

/// Drains serial-priority queues into UART while enforcing DIN pacing and strict priority gates.
void MidiQueueManager::flushSerialOutput(uint32_t nowSampleTimer) {
	if (!hasSerialData()) {
		// Fast exit when all lanes are empty; avoids pacing/space calculations.
		return;
	}

	// Apply DIN pacing before deciding this iteration's send allowance.
	updateSerialDinBudget(nowSampleTimer);

	int32_t uartSpace = uartGetTxBufferSpace(UART_ITEM_MIDI) - kSerialUartHeadroomBytes;
	if (uartSpace <= 0) {
		// Preserve a little headroom so other UART activity is not starved.
		return;
	}

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
		int32_t bytesPopped = popNextPrioritizedBytes(bytesToSend, 3, sendAllowanceBytes, uartSpace);
		if (bytesPopped <= 0) {
			break;
		}
		for (int32_t i = 0; i < bytesPopped; i++) {
			// Push selected bytes into the UART MIDI TX buffer.
			bufferMIDIUart(bytesToSend[i]);
		}
		sent += bytesPopped;
		uartSpace -= bytesPopped;
		sendAllowanceBytes -= bytesPopped;
	}

	if (sent > 0) {
		// Convert whole bytes back to Q8 units and debit pacing bucket.
		serialDinBudgetQ8_ = std::max<int32_t>(0, serialDinBudgetQ8_ - sent * 256);
	}
}
