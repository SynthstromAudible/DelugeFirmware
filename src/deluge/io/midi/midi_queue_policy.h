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
#include <cstdint>
#include <utility>

/// @brief Shared MIDI queue policy helpers used across transport-specific queue managers.
///
/// The transport managers store different queue units: USB stores one
/// packed USB-MIDI event per queue entry, while DIN stores raw serial bytes.
/// This class contains only the policy decisions that are independent of that
/// storage format: message classification, CC detection, and common gate checks.
class MIDIQueueManager {
public:
	/// @brief High nibble of a channel Control Change status byte (0xBn).
	static constexpr uint8_t k_channel_cc_status_nibble = 0x0B;
	/// @brief Byte length of a complete 3-byte channel-CC message.
	static constexpr int32_t k_channel_cc_message_length = 3;

	/// @brief Returns true for channel-CC status bytes (0xBn).
	/// @param status Raw MIDI status byte.
	/// @return True if the high nibble is the channel-CC status nibble.
	static constexpr bool is_channel_cc_status_byte(uint8_t status) {
		return (status >> 4) == k_channel_cc_status_nibble;
	}

	/// @brief Returns true for channel-CC status-type nibbles.
	/// @param status_type Status-type nibble (status byte with the channel bits masked off).
	/// @return True if @p status_type is the channel-CC status nibble.
	static constexpr bool is_channel_cc_status_type(uint8_t status_type) {
		return status_type == k_channel_cc_status_nibble;
	}

	/// @brief Returns true only for a complete 3-byte channel-CC message.
	/// @param status      Raw MIDI status byte.
	/// @param message_len Decoded byte length of the message.
	/// @return True if @p status is a channel-CC status byte and @p message_len is exactly 3.
	static constexpr bool is_three_byte_channel_cc(uint8_t status, int32_t message_len) {
		return message_len == k_channel_cc_message_length && is_channel_cc_status_byte(status);
	}

	/// @brief Outcome of adapting a scan step for scheduled-CC candidate selection.
	///
	/// @note Two of these four are inert as the code stands. `Skip` is neither produced by any scan
	///       adapter nor distinguished by `MIDICCQueuePolicy::select_scheduled_cc()` (which folds it into
	///       "not a candidate"). `Invalid` is consumed there but no adapter produces it: the only
	///       production scan adapter, in `MIDICCLanePolicy::pop_scheduled()`, returns just `NoMore` and
	///       `Candidate`. Left as-is deliberately; collapsing the enum is a separate change.
	enum class CandidateScanResult : uint8_t {
		/// Scan reached the end of the lane.
		NoMore,
		/// A valid message was seen, but it is not a scheduled-CC candidate.
		Skip,
		/// A scheduled-CC candidate was found.
		Candidate,
		/// The lane contents could not be safely reordered.
		Invalid,
	};

	/// @brief Outcome of adapting a scan step for CC coalescing.
	enum class CoalesceScanResult : uint8_t {
		/// Scan reached the end of the lane.
		NoMore,
		/// A valid message was seen, but it is not a coalescing candidate.
		Skip,
		/// A candidate with identity (status + CC number) usable for matching was found.
		Matchable,
		/// The lane contents could not be reliably matched.
		Invalid,
	};

	/// @brief Outcome of MIDIQueueManager::try_pop_scheduled_cc.
	enum class CCScheduledPopResult : uint8_t {
		/// The lane head is not a CC message; use normal lane-order popping.
		NotCC,
		/// The head is CC, but the caller's scheduling allowance is exhausted.
		AllowanceBlocked,
		/// The head is CC and allowance permits scheduling, but no candidate could be popped.
		PopFailed,
		/// A scheduled CC candidate was found and removed.
		Popped,
	};

	/// @brief Outcome of one priority-lane traversal step in a transport's dequeue loop.
	enum class PriorityLaneTraversalResult {
		/// Pop the current lane using normal lane-order popping.
		PopLane,
		/// The lane holds queued data but nothing eligible to pop this pass (for example, the CC
		/// scheduling allowance is exhausted); move on to the next priority lane. Callers only reach
		/// a lane after checking it is non-empty, so this never means "empty".
		SkipLane,
		/// A message was already popped (e.g. via the CC scheduler); stop traversing.
		Popped,
		/// Stop traversing for this slot without trying lower-priority lanes.
		///
		/// @note Only DIN ever produces this. `MIDIQueueManagerDIN::handle_cc_lane()` returns it for a
		///       head it cannot safely pop: one that does not decode, and also one that decodes fine but
		///       does not fit the current limits, because `validate_head_message_pop()` returning
		///       `InsufficientCapacity` is treated the same as `Invalid` there.
		/// @note `MIDIQueueManagerUSB::handle_cc_lane()` never returns this. USB entries are whole
		///       events with nothing to decode or to size-check, so it maps `PopFailed` to `SkipLane`
		///       and returns only `Popped` / `PopLane` / `SkipLane`. The `Abort` branch in
		///       `MIDIQueueManagerUSB::consume_queued_messages()` is therefore unreachable, and is kept
		///       only so the two traversal loops read the same way.
		Abort,
	};

	/// @brief Outcome of validate_head_message_pop.
	///
	/// @note The three-way split has no consumer today. Both call sites
	///       (`MIDIQueueManagerDIN::pop_lane()` and `MIDIQueueManagerDIN::handle_cc_lane()`) test only
	///       `!= Ready`, so `Invalid` and `InsufficientCapacity` behave identically — which is the
	///       mechanical root of the over-broad `Abort` documented above. Distinguishing them is a real
	///       behaviour change and wants its own failing test.
	enum class HeadMessageCheckResult : uint8_t {
		/// The head status byte does not decode to a valid message length.
		Invalid,
		/// The message decodes, but it does not fit queue occupancy or caller limits.
		InsufficientCapacity,
		/// The complete head message can be popped now.
		Ready,
	};

	/// @brief Classifies an outgoing MIDI message into a priority queue lane.
	///
	/// Defined inline rather than in the .cpp so it can be unit tested without the UART and USB layers.
	///
	/// @note Message intent is consumed here and nowhere else. Routing Event CCs away from the CC lane
	///       leaves that lane holding only Continuous entries, which is what lets the dequeue path
	///       coalesce and reorder it unconditionally without needing to know any entry's intent -
	///       there is nowhere to store intent per queue entry (a USB entry is a fully packed uint32,
	///       a DIN entry is a raw byte).
	/// @param message Outgoing MIDI message to classify.
	/// @return The priority lane this message belongs in.
	static QueuePriority classify_message(MIDIMessage message) {
		if (message.isSystemMessage()) {
			// Keep system/realtime messages in the highest-priority lane.
			return QUEUE_PRIORITY_CLOCK;
		}

		if (message.intent == MIDIIntent::NoteBound) {
			// Must stay ordered with the note stream, and lanes are FIFO, so it has to share the notes
			// lane. Covers MPE expression that initialises a note, and All Notes Off.
			return QUEUE_PRIORITY_NOTES;
		}

		switch (static_cast<MIDIStatusType>(message.statusType)) {
		case MIDIStatusType::NoteOff:
		case MIDIStatusType::NoteOn:
			// Note on/off events are timing-sensitive, but below clock/system messages.
			return QUEUE_PRIORITY_NOTES;

		case MIDIStatusType::PolyphonicAftertouch:
		case MIDIStatusType::ChannelAftertouch:
		case MIDIStatusType::PitchBend:
			// Expression data is important for feel, but can sit behind notes.
			return QUEUE_PRIORITY_EXPRESSION;

		case MIDIStatusType::ControlChange:
			if (message.data1 == CC_EXTERNAL_MOD_WHEEL || message.data1 == CC_EXTERNAL_MPE_Y) {
				// Mod wheel and MPE Y-axis are expressive CCs that should be prioritized above other CCs.
				return QUEUE_PRIORITY_EXPRESSION;
			}
			if (message.intent == MIDIIntent::Continuous) {
				// Only continuous parameter updates may be merged and reordered, so only they belong in
				// the scheduled CC lane.
				return QUEUE_PRIORITY_CC;
			}
			// Discrete CC events keep their order and their duplicate values. The expression lane is
			// strictly FIFO and never coalesced, which is exactly the behaviour they need.
			return QUEUE_PRIORITY_EXPRESSION;

		default:
			// Program change and unknown channel messages are discrete events that follow a prefix.
			return QUEUE_PRIORITY_EXPRESSION;
		}
	}

	/// @brief Shared CC gate helper: if the lane head is CC and allowance permits, attempt a scheduled pop.
	///
	/// A transport asks this before popping the CC lane head. Non-CC messages fall through to normal
	/// lane-order popping. CC messages only use the scheduler when the caller's per-transfer/per-UART
	/// allowance still permits it.
	///
	/// @param head_is_cc       True if the lane head is a CC message.
	/// @param allowance_ok     True if the caller's scheduling allowance still permits a scheduled pop.
	/// @param pop_scheduled_fn Transport callback that attempts to pop the scheduled CC candidate.
	/// @param args             Arguments forwarded to @p pop_scheduled_fn.
	/// @return Whether normal popping should run, and if not, whether the scheduled pop succeeded.
	template <typename PopFn, typename... Args>
	static CCScheduledPopResult try_pop_scheduled_cc(bool head_is_cc, bool allowance_ok, PopFn pop_scheduled_fn,
	                                                 Args&&... args) {
		if (!head_is_cc) {
			// The lane head is not a CC, so normal lane-order popping should handle it.
			return CCScheduledPopResult::NotCC;
		}
		if (!allowance_ok) {
			// CC scheduling is intentionally capped per transfer/flush.
			return CCScheduledPopResult::AllowanceBlocked;
		}
		if (pop_scheduled_fn(std::forward<Args>(args)...)) {
			// The transport found and removed the scheduled CC candidate.
			return CCScheduledPopResult::Popped;
		}
		// The head was CC and allowance was available, but the transport could not pop one.
		return CCScheduledPopResult::PopFailed;
	}

	/// @brief Shared parser+fit gate for queued non-realtime MIDI messages.
	///
	/// Returns Ready only when the queue head decodes to a valid message length and that complete
	/// message fits queue occupancy plus all caller limits.
	///
	/// @param status          Head status byte.
	/// @param queue_size      Bytes/entries currently available in the queue.
	/// @param allowance_bytes Caller's remaining send allowance, in bytes.
	/// @param uart_space      Remaining space in the UART/transport output buffer.
	/// @param max_len         Caller-imposed maximum message length for this pop.
	/// @param message_len_out Out: decoded message length, set only when the result is Ready.
	/// @return Whether the head message is invalid, does not currently fit, or is ready to pop.
	static HeadMessageCheckResult validate_head_message_pop(uint8_t status, uint16_t queue_size,
	                                                        int32_t allowance_bytes, int32_t uart_space,
	                                                        int32_t max_len, int32_t& message_len_out) {
		// Determine how many bytes must be emitted atomically for this status byte.
		int32_t message_len = bytesPerStatusMessage(status);
		if (message_len <= 0) {
			// A non-positive length means this byte cannot start a valid queued message.
			return HeadMessageCheckResult::Invalid;
		}
		if (queue_size < message_len || allowance_bytes < message_len || uart_space < message_len
		    || max_len < message_len) {
			// The message is recognizable, but the queue/caller cannot provide all bytes now.
			return HeadMessageCheckResult::InsufficientCapacity;
		}
		// Return the decoded length so the caller can pop exactly one complete message.
		message_len_out = message_len;
		return HeadMessageCheckResult::Ready;
	}
};
