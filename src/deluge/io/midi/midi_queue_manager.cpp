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
constexpr int32_t kSerialBufferedCCBytesCap = 24;
// Limit how many USB CC packets can be staged in a single transfer batch.
constexpr int32_t kUsbBufferedCCPacketsCap = 8;

/*
 * MIDI Queue Manager Information
 *
 * General (applies to serial and USB):
 * -----------------------------------
 * - Traffic is classified into shared priority lanes (clock, notes,
 *   expression, CC) to preserve musical responsiveness under contention.
 * - Scheduler behavior is deterministic and message-boundary-safe (a full message must be sent); queue
 *   mutation should not produce partial MIDI packets.
 *
 * Serial (DIN) only:
 * -------------------
 * - Tested using two Deluges connected via DIN, sending clock, notes, and 80+ automated CC's.
 *   Result: ensured that clock was stable, notes were auditioned in time and cc message
 *   values were received correctly
 *
 * - SysEx is excluded from this scheduler.
 *   Why: sysex transmissions were already managed separately and need to be sent in full.
 * - CC coalescing keeps only the newest pending value for matching
 *   channel/controller pairs.
 *   Why: this addresses observed stale/stuck CC outcomes where obsolete queued
 *   values were transmitted after newer intent.
 * - CC dequeue is fairness-aware across controller numbers (RR start + debt).
 *   Why: this addresses observed starvation where heavy controllers dominated
 *   service and delayed other controllers.
 * - CC dequeue is gated before mutation by full-message fit checks (budget,
 *   UART space, output span).
 *   Why: this prevents queue-state changes when bytes cannot be emitted,
 *   avoiding value loss/misalignment under load.
 * - Fair-selected CC removal is atomic even when selected away from queue head
 *   (offset-based rebuild of remaining bytes).
 *   Why: preserves ordering and avoids malformed/partial mutation side effects.
 * - Staged CC bytes in UART are capped.
 *   Why: this prevents excessive low-priority buffering that reduced
 *   responsiveness of higher-priority traffic.
 *
 * USB only:
 * -------------------
 * - Tested using two Deluges connected via USB, sending clock, notes, and 80+ automated CC's.
 *   Result: ensured that clock was stable, notes were auditioned in time and cc message
 *   values were received correctly
 * - USB output uses per-priority ring lanes and strict priority pop order
 *   (clock -> notes -> expression -> CC -> SysEx).
 *   Why: keeps transport/realtime and performance-critical messages from being
 *   delayed by lower-priority backlog on shared USB endpoints.
 * - USB channel-CC writes are coalesced by status+controller.
 *   Why: keeps only the newest pending value for each controller, preventing
 *   stale values from being sent after newer intent.
 * - USB channel-CC dequeue is fairness-aware (RR baseline + debt preference).
 *   Why: prevents heavy controllers from monopolizing CC service when many
 *   controllers are active at once.
 * - USB channel-CC selection/removal is atomic when selected away from lane
 *   head (offset-based packet rebuild).
 *   Why: preserves packet order and avoids malformed dequeue side effects.
 * - USB transfer assembly applies a per-batch CC packet cap.
 *   Why: bounds low-priority CC bursts so clock/notes can preempt quickly
 *   under sustained CC pressure.
 * - USB queue accounting is monotonic-counter based and lane-local.
 *   Why: this provides deterministic occupancy and stable dequeue behavior
 *   across wrap-around without expensive modulo-heavy state.
 */

/// Saturating increment for per-controller fairness debt.
///
/// Debt models relative enqueue pressure: controllers that accumulate more
/// unsent writes become more likely to be selected by fair dequeue.
inline void bumpControllerDebt(uint8_t* debt, uint8_t controller) {
	if (controller <= 127 && debt[controller] < 0xFF) {
		debt[controller]++;
	}
}

inline uint8_t usbStatusByte(uint32_t packed) {
	// USB-MIDI event packets store CIN in byte 0 and status in byte 1.
	return static_cast<uint8_t>((packed >> 8) & 0xFF);
}

inline uint8_t usbData1(uint32_t packed) {
	// Byte 2 is MIDI data1 for channel/system messages.
	return static_cast<uint8_t>((packed >> 16) & 0xFF);
}

inline uint8_t usbData2(uint32_t packed) {
	// Byte 3 is MIDI data2 (for CC this is the value byte).
	return static_cast<uint8_t>((packed >> 24) & 0xFF);
}

inline bool isUsbChannelCc(uint32_t packed) {
	// Channel-CC status family is 0xBn (high nibble 0x0B).
	return (usbStatusByte(packed) >> 4) == 0x0B;
}
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

/// Pops one USB packet using strict priority ordering, with fair CC selection.
bool MidiQueueManager::usbPopPriorityMessage(ConnectedUSBMIDIDevice* device, uint32_t& messageOut,
                                             int32_t& ccBudgetPacketsRemaining) {
	for (uint8_t p = QUEUE_PRIORITY_CLOCK; p < QUEUE_PRIORITY_COUNT; p++) {
		QueuePriority priority = static_cast<QueuePriority>(p);
		if (!usbQueueCount(device, priority)) {
			continue;
		}

		if (priority == QUEUE_PRIORITY_CC) {
			// Inspect the CC-lane head packet to decide whether CC fairness rules apply.
			uint16_t headIdx = device->ringBufReadIdx[p] & MIDI_SEND_RING_MASK;
			uint32_t headMessage = device->sendDataRingBuf[p][headIdx];

			if (isUsbChannelCc(headMessage)) {
				// Per-transfer CC cap prevents low-priority bursts from dominating the batch.
				if (ccBudgetPacketsRemaining <= 0) {
					// Skip CC for now and keep scanning lower lanes (e.g. SysEx) this pass.
					continue;
				}
				// Pop one fair-selected CC packet (RR baseline + debt preference).
				if (usbPopFairQueuedCcMessage(device, messageOut)) {
					// Charge one CC slot so the cap is enforced across this transfer assembly.
					ccBudgetPacketsRemaining--;
					return true;
				}
				// If fair pop failed, do not dequeue arbitrary CC head data in this call.
				continue;
			}
			// Non-CC packets living in the CC lane are handled by the generic dequeue path below.
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
	// For channel-CC, prefer updating an already-queued matching controller value over appending another packet.
	if (priority == QUEUE_PRIORITY_CC && usbCoalesceQueuedCc(device, message)) {
		return;
	}

	// Power-of-two mask wraps index without modulo cost.
	uint8_t p = static_cast<uint8_t>(priority);
	device->sendDataRingBuf[p][device->ringBufWriteIdx[p] & MIDI_SEND_RING_MASK] = message;
	device->ringBufWriteIdx[p]++;

	if (priority == QUEUE_PRIORITY_CC && isUsbChannelCc(message)) {
		// Extract controller number from data1 for fairness/debt accounting.
		uint8_t controller = usbData1(message);
		if (controller <= 127) {
			// Enqueued CC increases this controller's pressure in fair selection.
			bumpControllerDebt(device->usbCcFairControllerDebt.data(), controller);
			// Mark controller as currently represented in queued CC traffic.
			device->usbCcFairControllerPending[controller] = 1;
		}
	}
}

/// Coalesces queued USB channel-CC packets by controller/status.
///
/// Searches the USB CC lane for the newest pending packet with the same status
/// byte and controller number, then updates only that packet's value byte.
/// Returns `true` when an in-queue replacement was applied.
bool MidiQueueManager::usbCoalesceQueuedCc(ConnectedUSBMIDIDevice* device, uint32_t message) {
	// Coalescing is defined only for channel-CC packets; other message types must enqueue normally.
	if (!isUsbChannelCc(message)) {
		return false;
	}

	constexpr uint8_t p = QUEUE_PRIORITY_CC;
	uint16_t queueSize = usbQueueCount(device, QUEUE_PRIORITY_CC);
	if (!queueSize) {
		// No queued CC packets means there is nothing to coalesce in-place.
		return false;
	}

	uint8_t wantedStatus = usbStatusByte(message);
	uint8_t wantedController = usbData1(message);
	int32_t latestOffset = -1;

	// Walk the queued CC lane and remember the newest matching status/controller.
	for (uint16_t offset = 0; offset < queueSize; offset++) {
		// Ring read index + logical offset gives this packet's current queue position.
		uint32_t queued = device->sendDataRingBuf[p][(device->ringBufReadIdx[p] + offset) & MIDI_SEND_RING_MASK];
		if (isUsbChannelCc(queued) && usbStatusByte(queued) == wantedStatus && usbData1(queued) == wantedController) {
			// Keep updating so the final match is the latest pending packet.
			latestOffset = offset;
		}
	}

	if (latestOffset < 0) {
		// No matching queued status/controller pair was found; caller should enqueue a new packet.
		return false;
	}

	// Replace value byte in-place while preserving queue order for all packets.
	uint16_t targetIdx = (device->ringBufReadIdx[p] + latestOffset) & MIDI_SEND_RING_MASK;
	// Keep CIN/status/data1 (low 24 bits) and overwrite only data2 (high byte).
	device->sendDataRingBuf[p][targetIdx] =
	    (device->sendDataRingBuf[p][targetIdx] & 0x00FFFFFFu) | (static_cast<uint32_t>(usbData2(message)) << 24);
	// Treat this coalesced write as fresh controller pressure for fair dequeue.
	bumpControllerDebt(device->usbCcFairControllerDebt.data(), wantedController);
	// Ensure this controller remains marked as present in queued CC traffic.
	device->usbCcFairControllerPending[wantedController] = 1;
	return true;
}

/// Removes one queued USB CC packet at a logical offset, atomically.
///
/// Fair dequeue may target a packet that is not at the lane head. This helper
/// copies the selected packet out, rebuilds the remaining CC-lane order, and
/// resets lane cursors to the rebuilt image.
bool MidiQueueManager::usbRemoveQueuedCcMessageAtOffset(ConnectedUSBMIDIDevice* device, uint16_t targetOffset,
                                                        uint32_t& messageOut) {
	constexpr uint8_t p = QUEUE_PRIORITY_CC;
	uint16_t queueSize = usbQueueCount(device, QUEUE_PRIORITY_CC);
	if (targetOffset >= queueSize) {
		// Selected logical offset is outside current queue snapshot; cannot remove safely.
		return false;
	}

	// Translate logical queue offset into the wrapped physical ring index.
	uint16_t targetIdx = (device->ringBufReadIdx[p] + targetOffset) & MIDI_SEND_RING_MASK;
	// Return the selected packet so caller can emit/process it after atomic removal.
	messageOut = device->sendDataRingBuf[p][targetIdx];

	uint16_t scratchSize = 0;
	// Rebuild a compact queue image by copying every packet except the selected one.
	for (uint16_t i = 0; i < queueSize; i++) {
		if (i == targetOffset) {
			// Skip the target packet; it has already been captured in messageOut.
			continue;
		}
		// Preserve logical queue order while writing survivors into scratch storage.
		device->usbCcReorderScratch[scratchSize++] =
		    device->sendDataRingBuf[p][(device->ringBufReadIdx[p] + i) & MIDI_SEND_RING_MASK];
	}

	// Rebuild lane content without the selected packet to keep order deterministic.
	device->ringBufReadIdx[p] = 0;
	device->ringBufWriteIdx[p] = 0;
	for (uint16_t i = 0; i < scratchSize; i++) {
		// Replay compacted packets back into the lane in preserved logical order.
		device->sendDataRingBuf[p][device->ringBufWriteIdx[p] & MIDI_SEND_RING_MASK] = device->usbCcReorderScratch[i];
		// Advance write cursor after each restored packet.
		device->ringBufWriteIdx[p]++;
	}

	return true;
}

/// Pops one queued USB channel-CC packet using controller fairness.
///
/// Selection flow:
/// 1. Capture each controller's first queued CC offset.
/// 2. Establish RR baseline from rotating controller cursor.
/// 3. Prefer highest-debt controller when debt is non-zero.
/// 4. Remove selected packet atomically and commit fairness state.
bool MidiQueueManager::usbPopFairQueuedCcMessage(ConnectedUSBMIDIDevice* device, uint32_t& messageOut) {
	constexpr uint8_t p = QUEUE_PRIORITY_CC;
	uint16_t queueSize = usbQueueCount(device, QUEUE_PRIORITY_CC);
	if (!queueSize) {
		// No queued CC packets means there is nothing eligible for fair dequeue.
		return false;
	}

	auto& firstOffsets = device->usbCcFairFirstOffsets;
	// 0xFFFF marks "no queued packet found yet" for each controller.
	firstOffsets.fill(0xFFFF);
	// Tracks whether this queue snapshot contains any channel-CC packets at all.
	bool sawAnyCc = false;

	// Scan the current CC queue snapshot to collect first-seen offsets per controller.
	for (uint16_t offset = 0; offset < queueSize; offset++) {
		// Map logical scan offset to wrapped ring index, then inspect that queued packet.
		uint32_t queued = device->sendDataRingBuf[p][(device->ringBufReadIdx[p] + offset) & MIDI_SEND_RING_MASK];
		// Fair selection in this pass only considers channel-CC packets.
		if (!isUsbChannelCc(queued)) {
			continue;
		}
		sawAnyCc = true;
		// For channel-CC packets, data1 is the controller number used as fairness key.
		uint8_t controller = usbData1(queued);
		// Record only the first queued packet offset for each valid controller in this snapshot.
		if (controller <= 127 && firstOffsets[controller] == 0xFFFF) {
			firstOffsets[controller] = offset;
		}
	}

	// Without any channel-CC candidates in this snapshot, fair dequeue cannot select a packet.
	if (!sawAnyCc) {
		return false;
	}

	// Refresh pending flags so each controller reflects current queued-CC presence in this snapshot.
	for (uint16_t controller = 0; controller < 128; controller++) {
		device->usbCcFairControllerPending[controller] = (firstOffsets[controller] != 0xFFFF) ? 1 : 0;
	}

	// Candidate scratch state:
	// - firstRoundRobin*: first eligible controller encountered in rotated RR order.
	// - debtSelected*: highest-debt controller candidate found in the same sweep.
	// - selectionTick: next monotonic service epoch to commit on successful dequeue.
	uint16_t firstRoundRobinOffset = 0xFFFF;
	uint8_t firstRoundRobinController = 0;
	uint16_t debtSelectedOffset = 0xFFFF;
	uint8_t debtSelectedController = 0;
	uint8_t debtSelectedValue = 0;
	uint32_t selectionTick = device->usbCcFairServiceTick + 1;

	// Sweep all 128 controller slots starting at the rotating cursor to form fairness candidates.
	for (uint16_t search = 0; search < 128; search++) {
		// Rotate from the RR start cursor and wrap into MIDI controller domain [0,127].
		uint8_t controller = static_cast<uint8_t>((device->usbCcFairNextController + search) & 0x7f);
		// Sentinel means this controller has no queued CC candidate in this snapshot.
		uint16_t targetOffset = firstOffsets[controller];
		if (targetOffset == 0xFFFF) {
			// Skip controllers that are not currently represented in the queued CC lane.
			continue;
		}

		// Latch the first eligible hit in rotated order as the round-robin fallback candidate.
		if (firstRoundRobinOffset == 0xFFFF) {
			firstRoundRobinOffset = targetOffset;
			firstRoundRobinController = controller;
		}

		// Debt tracks relative enqueue pressure; keep the highest-debt eligible candidate.
		uint8_t debt = device->usbCcFairControllerDebt[controller];
		if (debtSelectedOffset == 0xFFFF || debt > debtSelectedValue) {
			debtSelectedOffset = targetOffset;
			debtSelectedController = controller;
			debtSelectedValue = debt;
		}
	}

	// No eligible controller was discovered, so fair dequeue has nothing to emit this pass.
	if (firstRoundRobinOffset == 0xFFFF) {
		return false;
	}

	// Default to RR baseline; override only when a valid debt candidate has positive pressure.
	uint16_t selectedOffset = firstRoundRobinOffset;
	uint8_t selectedController = firstRoundRobinController;
	if (debtSelectedOffset != 0xFFFF && debtSelectedValue > 0) {
		selectedOffset = debtSelectedOffset;
		selectedController = debtSelectedController;
	}

	// Do not commit fairness bookkeeping unless the selected packet is removed atomically.
	if (!usbRemoveQueuedCcMessageAtOffset(device, selectedOffset, messageOut)) {
		return false;
	}

	// Commit post-dequeue fairness state for the serviced controller, then rotate RR start.
	device->usbCcFairServiceTick = selectionTick;
	device->usbCcFairLastServedTick[selectedController] = selectionTick;
	device->usbCcFairControllerDebt[selectedController] = 0;
	device->usbCcFairControllerPending[selectedController] = 0;
	device->usbCcFairNextController = static_cast<uint8_t>((selectedController + 1) & 0x7f);
	// Selection and removal succeeded; caller can emit the selected packet.
	return true;
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
	// Consume one byte and wrap cursor within ring capacity.
	readPos = (readPos + 1) & (kCapacity - 1);
	return true;
}

/// Pops `count` bytes atomically from a serial-priority ring buffer lane.
bool MidiQueueManager::SerialByteQueue::popMany(uint8_t* out, uint16_t count) {
	// All-or-nothing pop to preserve complete MIDI message boundaries.
	if (size() < count) {
		return false;
	}
	// Copy the logical span out of the ring, wrapping with the capacity mask.
	for (uint16_t i = 0; i < count; i++) {
		out[i] = data[(readPos + i) & (kCapacity - 1)];
	}
	// Consume the copied span and wrap the cursor within ring capacity.
	readPos = (readPos + count) & (kCapacity - 1);
	return true;
}

/// Refills DIN serial pacing tokens from elapsed sample time.
void MidiQueueManager::updateSerialDinBudget(uint32_t nowSampleTimer) {
	uint32_t deltaSamples = nowSampleTimer - serialBudgetLastUpdate_;
	if (!deltaSamples) {
		// No elapsed sample time means no new transmit budget to accrue.
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
bool MidiQueueManager::removeQueuedCcMessageAtOffset(uint16_t targetOffset, uint8_t* outBytes) {
	SerialByteQueue& queue = serialPriorityQueues_[QUEUE_PRIORITY_CC];
	uint16_t queueSize = queue.size();
	if (targetOffset + 3 > queueSize) {
		// Selected CC span must be fully in-bounds of the current queue snapshot.
		return false;
	}

	// Fair selection can target a CC message in the middle of the byte ring,
	// so remove atomically by rebuilding the remaining bytes in-order.
	for (uint16_t i = 0; i < 3; i++) {
		outBytes[i] = queue.peek(targetOffset + i);
	}

	// Repack queue contents minus the selected 3-byte CC span.
	uint16_t scratchSize = 0;
	for (uint16_t i = 0; i < queueSize; i++) {
		if (i >= targetOffset && i < targetOffset + 3) {
			continue;
		}
		// Preserve byte order for all surviving queued messages.
		ccReorderScratch_[scratchSize++] = queue.peek(i);
	}

	// Reinitialize ring cursors, then replay compacted bytes as the new queue image.
	queue.readPos = 0;
	queue.writePos = 0;
	for (uint16_t i = 0; i < scratchSize; i++) {
		queue.push(ccReorderScratch_[i]);
	}

	return true;
}

/// Pops one queued CC message using controller-aware fairness.
///
/// Selection policy:
/// 1. Collect each controller's first queued CC offset.
/// 2. Start from `ccFairNextController_` for round-robin ordering.
/// 3. Prefer highest controller debt; use RR order as tie-break.
/// 4. Remove the selected message atomically via offset-based removal.
///
/// Returns `false` when budgets/space do not allow a full 3-byte CC message,
/// when queue data is malformed, or when no eligible CC is present.
bool MidiQueueManager::popFairQueuedCcMessage(uint8_t* outBytes, int32_t budgetBytes, int32_t uartSpace, int32_t maxLen,
                                              QueuePriority& poppedPriority) {
	// Fair CC dequeue is atomic on one full 3-byte message; bail out if any limiter
	// (DIN budget, UART space, or output span) cannot accommodate that minimum.
	if (budgetBytes < 3 || uartSpace < 3 || maxLen < 3) {
		return false;
	}

	SerialByteQueue& queue = serialPriorityQueues_[QUEUE_PRIORITY_CC];
	uint16_t queueSize = queue.size();
	// No possible CC candidate if fewer than 3 bytes are queued.
	if (queueSize < 3) {
		return false;
	}

	auto& firstOffsets = ccFairFirstOffsets_;
	// Sentinel marks "no queued packet for this controller" before this scan.
	firstOffsets.fill(0xFFFF);
	bool sawAnyCc = false;

	// Walk message-by-message so offsets always align to parsed MIDI frames, then
	// capture the first queued CC packet per controller for fair candidate selection.
	for (uint16_t offset = 0; offset < queueSize;) {
		uint8_t status = queue.peek(offset);
		int32_t messageLen = bytesPerStatusMessage(status);
		if (messageLen <= 0 || offset + messageLen > queueSize) {
			// Abort instead of consuming bytes from malformed/partial message boundaries.
			return false;
		}

		// Consider only canonical 3-byte channel-CC frames when building per-
		// controller dequeue candidates for fairness selection.
		if (messageLen == 3 && (status >> 4) == 0x0B) {
			sawAnyCc = true;
			uint8_t controller = queue.peek(offset + 1);
			// Accept only valid CC controller numbers and capture the first offset
			// per controller so selection operates on each controller's queue head.
			if (controller <= 127 && firstOffsets[controller] == 0xFFFF) {
				// Keep first occurrence only; later occurrences remain behind it in-order.
				firstOffsets[controller] = offset;
			}
		}

		// Step by one parsed MIDI frame to keep subsequent reads aligned.
		offset += static_cast<uint16_t>(messageLen);
	}

	if (!sawAnyCc) {
		// No eligible CC frames were discovered during the scan, so fairness
		// selection has no candidates to dequeue this pass.
		return false;
	}

	// Rebuild pending flags from this scan so fairness decisions use current
	// queue membership for every controller number.
	for (uint16_t controller = 0; controller < 128; controller++) {
		ccFairControllerPending_[controller] = (firstOffsets[controller] != 0xFFFF) ? 1 : 0;
	}

	// Candidate scratch state:
	// - firstRoundRobin*: first eligible candidate in rotating RR order.
	// - debtSelected*: highest-debt candidate discovered in the same scan.
	// - waitedSelectedValue: age metadata kept with debt-selected candidate.
	// - selectionTick: monotonic dequeue epoch used for waited-time math.
	uint16_t firstRoundRobinOffset = 0xFFFF;
	uint8_t firstRoundRobinController = 0;
	uint16_t debtSelectedOffset = 0xFFFF;
	uint8_t debtSelectedController = 0;
	uint8_t debtSelectedValue = 0;
	uint32_t waitedSelectedValue = 0;
	uint32_t selectionTick = ccFairServiceTick_ + 1;

	// Sweep the full controller space starting at the rotating cursor to
	// capture both the first RR candidate and the highest-debt candidate.
	for (uint16_t search = 0; search < 128; search++) {
		// Wrap the rotating start position across MIDI controller domain [0,127].
		uint8_t controller = static_cast<uint8_t>((ccFairNextController_ + search) & 0x7f);
		uint16_t targetOffset = firstOffsets[controller];
		if (targetOffset == 0xFFFF) {
			// No queued CC head for this controller in the current scan.
			continue;
		}

		// Latch the first eligible hit in rotated order as the RR baseline;
		// this is the fallback when no controller has positive debt.
		if (firstRoundRobinOffset == 0xFFFF) {
			firstRoundRobinOffset = targetOffset;
			firstRoundRobinController = controller;
		}

		// Snapshot this controller's fairness signals: enqueue pressure (debt),
		// last service epoch, and derived wait age for starvation visibility.
		uint8_t debt = ccFairControllerDebt_[controller];
		uint32_t lastServed = ccFairLastServedTick_[controller];
		uint32_t waited = (lastServed == 0) ? selectionTick : (selectionTick - lastServed);
		// Debt chooses "most backlogged" first; round-robin order breaks ties.
		if (debtSelectedOffset == 0xFFFF || debt > debtSelectedValue) {
			debtSelectedOffset = targetOffset;
			debtSelectedController = controller;
			debtSelectedValue = debt;
			waitedSelectedValue = waited;
		}
	}

	if (firstRoundRobinOffset == 0xFFFF) {
		// Sweep found no eligible controller candidate to dequeue this pass.
		return false;
	}

	// Start from RR baseline, then promote to debt-selected candidate only
	// when some controller has non-zero backlog pressure.
	uint16_t selectedOffset = firstRoundRobinOffset;
	uint8_t selectedController = firstRoundRobinController;
	uint8_t selectedDebt = ccFairControllerDebt_[selectedController];
	uint32_t selectedWaited = (ccFairLastServedTick_[selectedController] == 0)
	                              ? selectionTick
	                              : (selectionTick - ccFairLastServedTick_[selectedController]);
	if (debtSelectedOffset != 0xFFFF && debtSelectedValue > 0) {
		selectedOffset = debtSelectedOffset;
		selectedController = debtSelectedController;
		selectedDebt = debtSelectedValue;
		selectedWaited = waitedSelectedValue;
	}

	// Commit nothing unless the selected CC can be removed atomically.
	if (!removeQueuedCcMessageAtOffset(selectedOffset, outBytes)) {
		return false;
	}

	// Successful dequeue commits fairness bookkeeping: advance service epoch,
	// mark selected controller as served/cleared, and rotate RR start cursor.
	ccFairServiceTick_ = selectionTick;
	ccFairLastServedTick_[selectedController] = selectionTick;
	ccFairControllerDebt_[selectedController] = 0;
	ccFairControllerPending_[selectedController] = 0;
	poppedPriority = QUEUE_PRIORITY_CC;
	ccFairNextController_ = static_cast<uint8_t>((selectedController + 1) & 0x7f);
	return true;
}

/// Coalesces a queued CC by replacing the newest matching pending value.
///
/// For dense automation, multiple writes for the same channel/controller can
/// become stale before transmission. This function searches queued CC packets
/// for the latest matching status/controller and updates only its value byte,
/// reducing backlog without changing wire-order for other messages.
bool MidiQueueManager::coalesceQueuedCc(MIDIMessage message) {
	if (message.statusType != 0x0B) {
		// Only channel CC messages are eligible for in-queue value replacement.
		return false;
	}

	SerialByteQueue& queue = serialPriorityQueues_[QUEUE_PRIORITY_CC];
	uint16_t queueSize = queue.size();
	if (queueSize < 3) {
		// A complete CC frame is 3 bytes, so shorter queues cannot contain one.
		return false;
	}

	// Match only the same channel+status byte; sentinel -1 means no queued
	// packet for this controller/channel pair has been found yet.
	uint8_t wantedStatus = message.channel | (message.statusType << 4);
	int32_t latestMatchOffset = -1;

	for (uint16_t offset = 0; offset < queueSize;) {
		uint8_t status = queue.peek(offset);
		int32_t messageLen = bytesPerStatusMessage(status);
		if (messageLen <= 0 || offset + messageLen > queueSize) {
			// Stop coalescing scan on malformed boundary rather than mutating unknown bytes.
			break;
		}

		// Match only full 3-byte CC packets for the same status/channel and
		// controller number; those are eligible for in-place value replacement.
		if (messageLen == 3 && status == wantedStatus && queue.peek(offset + 1) == message.data1) {
			// Keep walking to coalesce the newest pending value for this controller.
			latestMatchOffset = offset;
		}

		offset += messageLen;
	}

	if (latestMatchOffset < 0) {
		// No pending packet for this controller/channel pair; caller may enqueue normally.
		return false;
	}

	// Patch only the value byte of the newest matching queued packet.
	uint16_t valueIndex = (queue.readPos + latestMatchOffset + 2) & (SerialByteQueue::kCapacity - 1);
	queue.data[valueIndex] = message.data2;
	// Treat coalesce as fresh pressure so fairness can compensate relative enqueue rate.
	bumpControllerDebt(ccFairControllerDebt_, message.data1);
	ccFairControllerPending_[message.data1] = 1;
	return true;
}

/// Enqueues a complete byte sequence into one serial-priority lane.
bool MidiQueueManager::enqueueSerialBytes(QueuePriority priority, uint8_t const* bytes, int32_t len) {
	SerialByteQueue& queue = serialPriorityQueues_[static_cast<uint8_t>(priority)];
	if (len <= 0) {
		// Empty payload is a successful no-op; callers can treat it as enqueued.
		return true;
	}
	if (queue.space() < len) {
		// Reject atomically if lane is full; never enqueue partial messages.
		return false;
	}
	// Space is guaranteed above, so this loop commits the whole message payload
	// into the ring in-order with no partial-enqueue path.
	for (int32_t i = 0; i < len; i++) {
		queue.push(bytes[i]);
	}
	return true;
}

/// Pops one realtime byte or one full MIDI message under budget and UART-space limits.
int32_t MidiQueueManager::popNextPrioritizedBytes(uint8_t* outBytes, int32_t maxLen, int32_t budgetBytes,
                                                  int32_t uartSpace, int32_t ccUartBudget,
                                                  QueuePriority& poppedPriority) {
	// Nothing can be emitted if caller buffer room, DIN token budget, or UART
	// space is already exhausted for this scheduling pass.
	if (maxLen <= 0 || budgetBytes <= 0 || uartSpace <= 0) {
		return 0;
	}

	constexpr size_t kClockIdx = QUEUE_PRIORITY_CLOCK;
	constexpr size_t kCcIdx = QUEUE_PRIORITY_CC;

	// Scan lanes in strict priority order (clock -> notes -> expression -> CC)
	// and stop at the first lane that can produce a full eligible payload.
	for (size_t idx = kClockIdx; idx <= kCcIdx; idx++) {
		SerialByteQueue& queue = serialPriorityQueues_[idx];
		if (queue.empty()) {
			// This lane has no work; advance to the next priority lane in-order.
			continue;
		}

		if (idx == kClockIdx) {
			// Realtime/clock lane is byte-oriented: emit at most one byte per call
			// and only when all output limiters can accept at least one byte.
			if (budgetBytes < 1 || uartSpace < 1 || maxLen < 1) {
				return 0;
			}
			queue.pop(outBytes[0]);
			// Report the lane and exact byte count so caller accounting stays correct.
			poppedPriority = static_cast<QueuePriority>(idx);
			return 1;
		}

		if (idx == kCcIdx) {
			// Parse the queue-head message once so we can gate CC dequeue safely.
			uint8_t status = queue.peek();
			int32_t messageLen = bytesPerStatusMessage(status);
			if (messageLen <= 0) {
				// Unknown head status: defer and try again on a later scheduler pass.
				return 0;
			}

			// Never dequeue a CC message if the staged-CC UART budget is exhausted.
			if ((status >> 4) == 0x0B && messageLen == 3 && ccUartBudget < 3) {
				return 0;
			}

			// The CC-priority lane may contain non-CC channel messages (e.g. program change).
			// Only run fair dequeue for actual 3-byte CC messages; otherwise use normal popMany below.
			if ((status >> 4) == 0x0B && messageLen == 3) {
				if (popFairQueuedCcMessage(outBytes, budgetBytes, uartSpace, maxLen, poppedPriority)) {
					return 3;
				}
			}
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
		// Keep this final guard even after fit checks: popMany is the atomic boundary
		// that guarantees we never emit a partial MIDI message if queue state shifts.
		if (!queue.popMany(outBytes, messageLen)) {
			return 0;
		}
		// Report both the source lane and actual byte count so the caller can
		// apply CC-specific bookkeeping and debit UART/token budgets correctly.
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
	QueuePriority priority = classifyMessage(message);
	if (priority == QUEUE_PRIORITY_CC && coalesceQueuedCc(message)) {
		// For dense CC streams, replace pending controller value instead of appending another packet.
		return;
	}

	// Atomic lane enqueue; false means lane full and message is intentionally dropped.
	bool queuedOk = enqueueSerialBytes(priority, rawBytes, messageLength);

	if (priority == QUEUE_PRIORITY_CC) {
		if (!queuedOk) {
			// Do not update fairness state for data that never entered the queue.
			return;
		}
		// Debt tracks relative enqueue/coalesce pressure so dequeue can compensate fairly.
		uint8_t controller = message.data1;
		if (controller <= 127) {
			// Mark this controller as newly backlogged and currently present in the
			// queued CC set so fair dequeue can prioritize/consider it next pass.
			bumpControllerDebt(ccFairControllerDebt_, controller);
			ccFairControllerPending_[controller] = 1;
		}
	}
}

/// Drains serial-priority queues into UART while enforcing DIN pacing and strict priority gates.
void MidiQueueManager::flushSerialOutput(uint32_t nowSampleTimer) {
	if (!hasSerialData()) {
		// Fast exit when all lanes are empty; avoids pacing/space calculations.
		return;
	}

	// Apply DIN pacing before deciding this iteration's send allowance.
	updateSerialDinBudget(nowSampleTimer);

	int32_t rawUartSpace = uartGetTxBufferSpace(UART_ITEM_MIDI);
	int32_t uartSpace = rawUartSpace - kSerialUartHeadroomBytes;
	if (uartSpace <= 0) {
		// Preserve a little headroom so other UART activity is not starved.
		return;
	}
	// This counts CC bytes already staged in the UART path and limits additional
	// queued CC so later clock/notes can still preempt promptly.
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
		int32_t bytesPopped =
		    popNextPrioritizedBytes(bytesToSend, 3, sendAllowanceBytes, uartSpace, ccUartBudget, poppedPriority);
		if (bytesPopped <= 0) {
			break;
		}

		for (int32_t i = 0; i < bytesPopped; i++) {
			// Push selected bytes into the UART MIDI TX buffer.
			bufferMIDIUart(bytesToSend[i]);
		}
		sent += bytesPopped;

		bool isCcMessage = (poppedPriority == QUEUE_PRIORITY_CC);

		if (isCcMessage) {
			// Decrement staged-CC budget only for actual CC-lane output.
			ccUartBudget -= bytesPopped;
		}
		// Only commit fairness state when a full 3-byte channel-CC frame with a
		// valid controller number has actually been emitted to UART.
		if (isCcMessage && bytesPopped == 3 && (bytesToSend[0] >> 4) == 0x0B && bytesToSend[1] <= 127) {
			uint8_t dequeuedController = bytesToSend[1];
			// Successful transmit repays that controller's pressure and clears pending marker.
			ccFairControllerDebt_[dequeuedController] = 0;
			ccFairControllerPending_[dequeuedController] = 0;
		}
		uartSpace -= bytesPopped;
		sendAllowanceBytes -= bytesPopped;
	}

	if (sent > 0) {
		// Convert whole bytes back to Q8 units and debit pacing bucket.
		serialDinBudgetQ8_ = std::max<int32_t>(0, serialDinBudgetQ8_ - sent * 256);
	}
}
