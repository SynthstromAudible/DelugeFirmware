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

#include <limits>

namespace {
constexpr uint16_t k_no_controller_offset = 0xFFFF;
constexpr uint8_t k_max_controller_debt = std::numeric_limits<uint8_t>::max();

template <typename DebtContainer>
inline void commit_fair_controller_service_impl(DebtContainer& controller_debt, uint8_t& next_controller,
                                                uint8_t selected_controller) {
	if (selected_controller <= kMaxMIDIValue) {
		controller_debt[selected_controller] = 0;
	}
	next_controller = static_cast<uint8_t>((selected_controller + 1) & kMaxMIDIValue);
}
} // namespace

/*
 * MIDI Queue Manager Information
 *
 * This file intentionally keeps only shared queue policy and helper routines.
 * Transport-specific implementations live in queue_manager_types/ and call back into
 * this class for common behavior.
 */

/// Classifies an outgoing MIDI message into shared queue priorities.
QueuePriority MIDIQueueManager::classify_message(MIDIMessage message) {
	if (message.isSystemMessage()) {
		// Keep transport / realtime bytes at the highest priority lane.
		return QUEUE_PRIORITY_CLOCK;
	}

	switch (static_cast<MIDIStatusType>(message.statusType)) {
	case MIDIStatusType::NoteOff:
	case MIDIStatusType::NoteOn:
		return QUEUE_PRIORITY_NOTES;

	case MIDIStatusType::PolyphonicAftertouch:
	case MIDIStatusType::ChannelAftertouch:
	case MIDIStatusType::PitchBend:
		return QUEUE_PRIORITY_EXPRESSION;

	case MIDIStatusType::ControlChange:
		if (message.data1 == 1 || message.data1 == 74) {
			return QUEUE_PRIORITY_EXPRESSION;
		}
		return QUEUE_PRIORITY_CC;

	default:
		return QUEUE_PRIORITY_CC;
	}
}

/// Saturating increment for per-controller fairness debt.
///
/// Debt models relative enqueue pressure: controllers that accumulate more
/// unsent writes become more likely to be selected by fair dequeue.
void MIDIQueueManager::bump_controller_debt(uint8_t* debt, uint8_t controller) {
	if (controller <= kMaxMIDIValue && debt[controller] < k_max_controller_debt) {
		debt[controller]++;
	}
}

/// Selects one controller candidate using RR baseline with debt override.
///
/// - RR baseline: first eligible controller encountered in rotated order.
/// - Debt override: if any eligible controller has positive debt, pick the
///   highest-debt one (rotation order implicitly breaks ties).
bool MIDIQueueManager::select_fair_controller_candidate(std::array<uint16_t, kMaxMIDIValue + 1> const& first_offsets,
                                                        uint8_t next_controller, uint8_t const* controller_debt,
                                                        uint16_t& selected_offset, uint8_t& selected_controller) {
	uint16_t first_round_robin_offset = k_no_controller_offset;
	uint8_t first_round_robin_controller = 0;
	uint16_t debt_selected_offset = k_no_controller_offset;
	uint8_t debt_selected_controller = 0;
	uint8_t debt_selected_value = 0;

	for (uint16_t search = 0; search < (kMaxMIDIValue + 1); search++) {
		// Rotate from the RR start cursor and wrap into MIDI controller domain [0,kMaxMIDIValue].
		uint8_t controller = static_cast<uint8_t>((next_controller + search) & kMaxMIDIValue);
		// Sentinel means this controller has no queued CC candidate in this snapshot.
		uint16_t target_offset = first_offsets[controller];
		if (target_offset == k_no_controller_offset) {
			continue;
		}

		// Latch the first eligible hit in rotated order as the round-robin fallback candidate.
		if (first_round_robin_offset == k_no_controller_offset) {
			first_round_robin_offset = target_offset;
			first_round_robin_controller = controller;
		}

		// Debt tracks relative enqueue pressure; keep the highest-debt eligible candidate.
		uint8_t debt = controller_debt[controller];
		if (debt_selected_offset == k_no_controller_offset || debt > debt_selected_value) {
			debt_selected_offset = target_offset;
			debt_selected_controller = controller;
			debt_selected_value = debt;
		}
	}

	if (first_round_robin_offset == k_no_controller_offset) {
		return false;
	}

	// Default to RR baseline; override only when a valid debt candidate has positive pressure.
	selected_offset = first_round_robin_offset;
	selected_controller = first_round_robin_controller;
	if (debt_selected_offset != k_no_controller_offset && debt_selected_value > 0) {
		selected_offset = debt_selected_offset;
		selected_controller = debt_selected_controller;
	}

	return true;
}

/// Initializes per-controller first-offset snapshot state to "no offset found" and returns the same array.
std::array<uint16_t, kMaxMIDIValue + 1>&
MIDIQueueManager::initialize_first_controller_offsets(std::array<uint16_t, kMaxMIDIValue + 1>& first_offsets) {
	first_offsets.fill(k_no_controller_offset);
	return first_offsets;
}

/// Records a controller's first queued CC offset once per queue snapshot.
void MIDIQueueManager::record_first_controller_offset(std::array<uint16_t, kMaxMIDIValue + 1>& first_offsets,
                                                      uint8_t controller, uint16_t offset) {
	// Accept only valid CC controller numbers and capture the first offset
	// per controller so selection operates on each controller's queue head.
	if (controller <= kMaxMIDIValue && first_offsets[controller] == k_no_controller_offset) {
		// Keep first occurrence only; later occurrences remain behind it in-order.
		first_offsets[controller] = offset;
	}
}

/// Commits fair-dequeue service for one controller: clear debt and rotate RR cursor.
void MIDIQueueManager::commit_fair_controller_service(std::array<uint8_t, kMaxMIDIValue + 1>& controller_debt,
                                                      uint8_t& next_controller, uint8_t selected_controller) {
	commit_fair_controller_service_impl(controller_debt, next_controller, selected_controller);
}

void MIDIQueueManager::commit_fair_controller_service(uint8_t (&controller_debt)[kMaxMIDIValue + 1],
                                                      uint8_t& next_controller, uint8_t selected_controller) {
	commit_fair_controller_service_impl(controller_debt, next_controller, selected_controller);
}
