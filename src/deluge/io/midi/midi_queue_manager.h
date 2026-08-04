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

#pragma once

#include "definitions_cxx.hpp"
#include "io/midi/midi_queue_definitions.h"
#include "model/midi/message.h"
#include <array>

class ConnectedUSBMIDIDevice;

/// Shared MIDI queue policy and queue-lane helpers used by engine/device manager.
///
/// Recent queue-manager behavior changes are intentionally documented here:
/// 1. Serial queuing now excludes SysEx and only manages clock/notes/expression/CC lanes.
/// 2. CC enqueue path coalesces repeated controller writes to reduce stale automation backlog.
/// 3. CC dequeue uses controller-aware fairness with round-robin tie-breaking.
/// 4. CC dequeue is gated by staged-UART CC budget before queue mutation to avoid pop-without-send.
class MidiQueueManager {
public:
	MidiQueueManager();

	/// Classifies an outgoing MIDI message into priority groups.
	static QueuePriority classifyMessage(MIDIMessage message);

	/// Returns queued packet count for one USB priority lane.
	static uint16_t usbQueueCount(ConnectedUSBMIDIDevice const* device, QueuePriority priority);
	/// Returns total queued packet count across all USB priority lanes.
	static uint32_t usbTotalQueuedMessages(ConnectedUSBMIDIDevice const* device);
	/// Pops one queued packet according to strict USB priority ordering.
	static bool usbPopPriorityMessage(ConnectedUSBMIDIDevice* device, uint32_t& messageOut);
	/// Pushes one packet into the given USB priority lane.
	static void usbPushPriorityMessage(ConnectedUSBMIDIDevice* device, QueuePriority priority, uint32_t message);
	/// Clears all USB queue lanes/read-write cursors in `storage`.
	static void resetUsbQueueStorage(ConnectedUSBMIDIDevice* device);

	/// Resets serial queue pacing state to a known baseline.
	void resetSerialState(uint32_t nowSampleTimer);
	/// Returns whether any serial-priority lane has pending bytes.
	[[nodiscard]] bool hasSerialData() const;
	/// Queues one channel/system MIDI message into the serial-priority queues.
	void enqueueSerialMidiMessage(MIDIMessage message);
	/// Drains serial-priority queues into UART under pacing and priority rules.
	void flushSerialOutput(uint32_t nowSampleTimer);

private:
	/// Power-of-two byte ring used by each serial priority lane.
	struct SerialByteQueue {
		static constexpr uint16_t kCapacity = 512;

		std::array<uint8_t, kCapacity> data{};
		uint16_t readPos{0};
		uint16_t writePos{0};

		[[nodiscard]] bool empty() const { return readPos == writePos; }
		[[nodiscard]] uint16_t size() const { return (writePos - readPos) & (kCapacity - 1); }
		[[nodiscard]] uint16_t space() const { return (kCapacity - 1) - size(); }
		[[nodiscard]] uint8_t peek(uint16_t offset = 0) const { return data[(readPos + offset) & (kCapacity - 1)]; }
		bool push(uint8_t byte);
		bool pop(uint8_t& out);
		bool popMany(uint8_t* out, uint16_t count);
	};

	/// Serial queuing uses only clock > notes > expression > CC (SysEx bypasses this manager).
	static constexpr size_t kSerialPriorityCount = QUEUE_PRIORITY_CC + 1;
	std::array<SerialByteQueue, kSerialPriorityCount> serialPriorityQueues_{};
	/// Last sample-timer timestamp used for DIN pacing token updates.
	uint32_t serialBudgetLastUpdate_{0};
	/// Q8 fixed-point token bucket of currently permitted DIN bytes.
	int32_t serialDinBudgetQ8_{0};
	/// Scratch storage used when removing a fair-selected CC message from the byte ring.
	std::array<uint8_t, SerialByteQueue::kCapacity> ccReorderScratch_{};
	/// Scratch offsets used when selecting the next fair CC message.
	std::array<uint16_t, 128> ccFairFirstOffsets_{};
	/// Next controller number to prefer when rotating CC dequeue fairness.
	uint8_t ccFairNextController_{0};
	/// Approximate enqueue/coalesce pressure per controller since last dequeue.
	uint8_t ccFairControllerDebt_[128]{};
	/// Whether a controller currently has at least one queued CC message.
	uint8_t ccFairControllerPending_[128]{};
	/// Monotonic fair-service tick incremented for each dequeued CC message.
	uint32_t ccFairServiceTick_{0};
	/// Last fair-service tick when each controller was dequeued.
	uint32_t ccFairLastServedTick_[128]{};

	/// Refills DIN pacing tokens from elapsed sample time.
	void updateSerialDinBudget(uint32_t nowSampleTimer);
	/// Replaces the newest queued CC value for the same status/controller, if present.
	bool coalesceQueuedCc(MIDIMessage message);
	/// Removes and returns one queued CC message using controller round-robin fairness.
	bool popFairQueuedCcMessage(uint8_t* outBytes, int32_t budgetBytes, int32_t uartSpace, int32_t maxLen,
	                            QueuePriority& poppedPriority);
	/// Removes a CC message at the given byte offset by rebuilding the ring contents.
	bool removeQueuedCcMessageAtOffset(uint16_t targetOffset, uint8_t* outBytes);
	/// Attempts to enqueue a full byte sequence atomically into one priority lane.
	bool enqueueSerialBytes(QueuePriority priority, uint8_t const* bytes, int32_t len);
	/// Pops one realtime byte or one complete MIDI message according to lane priority.
	int32_t popNextPrioritizedBytes(uint8_t* outBytes, int32_t maxLen, int32_t budgetBytes, int32_t uartSpace,
	                                int32_t ccUartBudget, QueuePriority& poppedPriority);
};

extern MidiQueueManager midiQueueManager;
