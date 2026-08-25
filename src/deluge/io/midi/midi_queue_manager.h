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
#include <limits>
#include <utility>

template <typename T, size_t LaneCount, uint16_t const (&Capacities)[LaneCount]>
class MIDIQueueManagerDeviceState;

/// @brief Shared MIDI queue policy helpers used across transport-specific queue managers.
///
/// The transport managers below store different queue units: USB stores one
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

	/// @brief Outcome of one transport-specific CC lane scan step.
	enum class CCMessageScanResult : uint8_t {
		/// Scan reached the end of the lane.
		NoMore,
		/// A valid message was seen, but it is not a CC scan candidate.
		Skip,
		/// A CC message was found; see the accompanying CCMessageScanEntry.
		Found,
		/// The lane contents could not be decoded.
		Invalid,
	};

	/// @brief Transport-neutral view of one scanned CC message.
	struct CCMessageScanEntry {
		/// Logical offset of the message within its lane.
		uint16_t offset;
		/// Message status byte.
		uint8_t status;
		/// Control Change number (status byte's data1).
		uint8_t cc_number;
	};

	/// @brief Outcome of adapting a scan step for scheduled-CC candidate selection.
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
		/// @note The transports apply this differently for the same trigger. USB returns SkipLane when
		///       the CC allowance is exhausted or a scheduled pop fails, falling through to SysEx; DIN
		///       returns Abort for those same conditions as well as for a head it cannot decode, so a
		///       blocked DIN CC lane halts the whole pass rather than falling through.
		Abort,
	};

	/// @brief Outcome of validate_head_message_pop.
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

	/// @brief Converts a transport CC scan entry into the candidate shape used by scheduled dequeue.
	/// @param scan_result      Result of the underlying transport CC lane scan step.
	/// @param message          Scanned message data, valid when @p scan_result is Found.
	/// @param candidate_offset Out: logical offset of the candidate, set when a Candidate is returned.
	/// @param cc_number        Out: CC number of the candidate, set when a Candidate is returned.
	/// @return The scan step translated into scheduled-dequeue candidate terms.
	static CandidateScanResult adapt_cc_candidate_scan_result(CCMessageScanResult scan_result,
	                                                          CCMessageScanEntry const& message,
	                                                          uint16_t& candidate_offset, uint8_t& cc_number) {
		switch (scan_result) {
		case CCMessageScanResult::NoMore:
			// The transport scan reached the end of the lane.
			return CandidateScanResult::NoMore;
		case CCMessageScanResult::Skip:
			// The scan saw a valid message, but it is not a scheduled-CC candidate.
			return CandidateScanResult::Skip;
		case CCMessageScanResult::Found:
			// Copy the transport-neutral fields the scheduler needs to choose a CC.
			cc_number = message.cc_number;
			candidate_offset = message.offset;
			return CandidateScanResult::Candidate;
		case CCMessageScanResult::Invalid:
			// Invalid scan state means the caller should stop and avoid repacking.
			return CandidateScanResult::Invalid;
		}

		// Defensive fallback for compilers that do not prove the enum switch is exhaustive.
		return CandidateScanResult::Invalid;
	}

	/// @brief Converts a transport CC scan entry into the match shape used by CC coalescing.
	/// @param scan_result      Result of the underlying transport CC lane scan step.
	/// @param message          Scanned message data, valid when @p scan_result is Found.
	/// @param candidate_offset Out: logical offset of the match, set when Matchable is returned.
	/// @param status           Out: status byte of the match, set when Matchable is returned.
	/// @param cc_number        Out: CC number of the match, set when Matchable is returned.
	/// @return The scan step translated into coalescing-match terms.
	static CoalesceScanResult adapt_cc_coalesce_scan_result(CCMessageScanResult scan_result,
	                                                        CCMessageScanEntry const& message,
	                                                        uint16_t& candidate_offset, uint8_t& status,
	                                                        uint8_t& cc_number) {
		switch (scan_result) {
		case CCMessageScanResult::NoMore:
			// The transport scan reached the end of the lane.
			return CoalesceScanResult::NoMore;
		case CCMessageScanResult::Skip:
			// The scan saw a valid message, but it is not a coalescing candidate.
			return CoalesceScanResult::Skip;
		case CCMessageScanResult::Found:
			// Copy enough identity to match exact status/channel plus CC number.
			status = message.status;
			cc_number = message.cc_number;
			candidate_offset = message.offset;
			return CoalesceScanResult::Matchable;
		case CCMessageScanResult::Invalid:
			// Invalid scan state means no reliable match can be made from here.
			return CoalesceScanResult::Invalid;
		}

		// Defensive fallback for compilers that do not prove the enum switch is exhaustive.
		return CoalesceScanResult::Invalid;
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

/// @brief Stateful CC coalescing and scheduling policy instantiated per transport/device.
///
/// Dense automation can repeatedly queue the same CC number faster than the
/// MIDI link can drain it. The policy balances two goals:
/// 1. Coalesce queued CCs for the same status/CC number so stale intermediate
///    values do not waste bandwidth.
/// 2. Track "debt" for CC numbers that were coalesced or newly queued, then
///    prefer debt-bearing CC numbers when choosing the next scheduled CC.
///
/// Transport-specific code supplies scan and removal callbacks because USB and
/// DIN queues have different entry formats.
class MIDICCQueuePolicy {
public:
	/// @brief Constructs the policy with cleared debt/selection state.
	MIDICCQueuePolicy() { reset(); }

	/// @brief Clears scan scratch state, CC debt, and CC-number selection history.
	void reset() {
		// Drop any pending preference accumulated by queued/coalesced CC updates.
		cc_debt.fill(0);
		// Restart round-robin selection from CC 0.
		next_cc_number = 0;
	}

	/// @brief Chooses the next CC to emit in a single fused pass over the lane.
	///
	/// Selection follows the queued order for each CC number (the first entry for a CC number wins) and
	/// then prefers the CC with the most debt, falling back to round-robin from `next_cc_number`.
	///
	/// @note This deliberately does the whole job in one traversal. The previous shape filled a
	///       128-entry offset map, scanned the lane, then walked all 128 CC numbers to pick a winner -
	///       per pop, up to eight times per transfer, inside the interrupt-masked section of
	///       flushUSBMIDIOutput(). Folding selection into the scan drops that to O(lane occupancy) with
	///       a four-word bitmask clear, and costs nothing extra when the lane is short, which is the
	///       common case once coalescing has collapsed repeated CC updates.
	///
	/// @param begin_scan         Transport callback that initializes a scan over the CC lane.
	/// @param next_scan          Transport callback that advances the scan and yields one candidate.
	/// @param selected_offset    Out: logical offset of the selected CC, set only on success.
	/// @param selected_cc_number Out: CC number of the selected CC, set only on success.
	/// @return True if a CC candidate was selected; false if the lane had none or was malformed.
	template <typename BeginScanFn, typename NextScanFn>
	bool select_scheduled_cc(BeginScanFn&& begin_scan, NextScanFn&& next_scan, uint16_t& selected_offset,
	                         uint8_t& selected_cc_number) {
		uint16_t scan_position = 0;
		uint16_t limit = 0;
		if (!begin_scan(scan_position, limit)) {
			// The transport has no CC lane entries available to scan.
			return false;
		}

		// Tracks which CC numbers already contributed a candidate, so only the first entry per CC number
		// is eligible and a later distinct CC cannot jump ahead of an earlier one in the same bucket.
		seen_mask.fill(0);

		// Best candidate in round-robin order, used when nothing has debt.
		uint16_t rr_offset = k_no_cc_offset;
		uint8_t rr_cc_number = 0;
		uint8_t rr_rank = 0;
		// Best candidate by debt, which wins whenever any candidate has debt.
		uint16_t debt_offset = k_no_cc_offset;
		uint8_t debt_cc_number = 0;
		uint8_t debt_value = 0;
		uint8_t debt_rank = 0;

		while (true) {
			uint16_t candidate_offset = 0;
			uint8_t cc_number = 0;
			MIDIQueueManager::CandidateScanResult step = next_scan(scan_position, limit, candidate_offset, cc_number);
			if (step == MIDIQueueManager::CandidateScanResult::NoMore) {
				break;
			}
			if (step == MIDIQueueManager::CandidateScanResult::Invalid) {
				// A malformed lane cannot be safely reordered.
				return false;
			}
			if (step != MIDIQueueManager::CandidateScanResult::Candidate || cc_number > kMaxMIDIValue) {
				continue;
			}
			if (test_and_set_seen(cc_number)) {
				// A later entry for a CC number already represented by an earlier one.
				continue;
			}

			// Rank orders CC numbers starting at next_cc_number and wrapping, so ties resolve the same way
			// the previous round-robin walk did.
			uint8_t rank = static_cast<uint8_t>((cc_number - next_cc_number) & kMaxMIDIValue);
			if (rr_offset == k_no_cc_offset || rank < rr_rank) {
				rr_offset = candidate_offset;
				rr_cc_number = cc_number;
				rr_rank = rank;
			}

			uint8_t debt = cc_debt[cc_number];
			if (debt_offset == k_no_cc_offset || debt > debt_value || (debt == debt_value && rank < debt_rank)) {
				debt_offset = candidate_offset;
				debt_cc_number = cc_number;
				debt_value = debt;
				debt_rank = rank;
			}
		}

		if (rr_offset == k_no_cc_offset) {
			// The scan found no CC candidates at all.
			return false;
		}

		// Debt overrides round-robin so a control being actively moved is emitted promptly.
		if (debt_offset != k_no_cc_offset && debt_value > 0) {
			selected_offset = debt_offset;
			selected_cc_number = debt_cc_number;
		}
		else {
			selected_offset = rr_offset;
			selected_cc_number = rr_cc_number;
		}
		return true;
	}

	/// @brief Finds the newest queued CC matching a status/CC number pair.
	///
	/// Enqueue-time coalescing intentionally updates the latest match, preserving
	/// the queued order while ensuring the eventual send uses the freshest value.
	///
	/// @param wanted_status    Status byte to match.
	/// @param wanted_cc_number CC number to match.
	/// @param begin_scan       Transport callback that initializes a scan over the CC lane.
	/// @param next_scan        Transport callback that advances the scan and yields one candidate.
	/// @return Logical offset of the newest match, or -1 if none was found.
	template <typename BeginScanFn, typename NextScanFn>
	int32_t find_latest_matching_cc_offset(uint8_t wanted_status, uint8_t wanted_cc_number, BeginScanFn&& begin_scan,
	                                       NextScanFn&& next_scan) const {
		uint16_t scan_position = 0;
		uint16_t limit = 0;
		if (!begin_scan(scan_position, limit)) {
			// The transport has no CC lane entries available to scan.
			return -1;
		}

		int32_t latest_offset = -1;
		while (true) {
			// Walk each transport-specific CC scan entry and keep replacing
			// latest_offset when we find a newer exact status/CC match.
			uint16_t candidate_offset = 0;
			uint8_t status = 0;
			uint8_t cc_number = 0;
			MIDIQueueManager::CoalesceScanResult step =
			    next_scan(scan_position, limit, candidate_offset, status, cc_number);
			if (step == MIDIQueueManager::CoalesceScanResult::NoMore) {
				// End of lane: return the newest match found, or -1 if there was none.
				break;
			}
			if (step == MIDIQueueManager::CoalesceScanResult::Invalid) {
				// Stop on malformed queue contents; any match already found is still usable.
				break;
			}
			if (step == MIDIQueueManager::CoalesceScanResult::Matchable && status == wanted_status
			    && cc_number == wanted_cc_number) {
				// Keep the most recent matching offset so the freshest queued value is overwritten.
				latest_offset = static_cast<int32_t>(candidate_offset);
			}
		}

		return latest_offset;
	}

	/// @brief Finds a matching queued CC, lets the transport update it, then records CC debt.
	///
	/// USB and DIN store different queue units, so the policy does not know how
	/// to rewrite the queued value byte. The transport callback receives the
	/// logical offset of the latest matching CC and performs that overwrite.
	///
	/// @param wanted_status    Status byte to match.
	/// @param wanted_cc_number CC number to match.
	/// @param begin_scan       Transport callback that initializes a scan over the CC lane.
	/// @param next_scan        Transport callback that advances the scan and yields one candidate.
	/// @param update_matched   Transport callback that overwrites the matched entry's value byte.
	/// @return True if a match was found and updated; false if nothing was queued to coalesce into.
	template <typename BeginScanFn, typename NextScanFn, typename UpdateMatchedFn>
	bool coalesce_latest_matching_cc(uint8_t wanted_status, uint8_t wanted_cc_number, BeginScanFn&& begin_scan,
	                                 NextScanFn&& next_scan, UpdateMatchedFn&& update_matched) {
		// Search the lane for the newest queued value for this exact status/CC pair.
		int32_t latest_offset =
		    find_latest_matching_cc_offset(wanted_status, wanted_cc_number, std::forward<BeginScanFn>(begin_scan),
		                                   std::forward<NextScanFn>(next_scan));
		if (latest_offset < 0) {
			// Nothing already queued, so the caller should append a new message instead.
			return false;
		}

		// Let the transport rewrite the value byte in its own storage format. It re-validates the offset
		// under a critical section and reports a miss if the consumer removed the entry in the meantime,
		// in which case the caller must append a fresh message rather than dropping this update.
		if (!update_matched(static_cast<uint16_t>(latest_offset))) {
			return false;
		}
		// Record that this CC changed while queued, so scheduled dequeue prefers it.
		bump_cc_debt(wanted_cc_number);
		return true;
	}

	/// @brief Selects and removes the next scheduled CC candidate from a transport-specific lane.
	///
	/// The lane scan discovers candidates, the policy chooses the CC number,
	/// and the transport callback removes the selected queue span.
	///
	/// @param begin_scan      Transport callback that initializes a scan over the CC lane.
	/// @param next_scan       Transport callback that advances the scan and yields one candidate.
	/// @param remove_selected Transport callback that removes the selected queue span.
	/// @param out_arg         Extra argument forwarded to @p remove_selected (e.g. an output destination).
	/// @return True if a candidate was selected and removed; false otherwise.
	template <typename BeginScanFn, typename NextScanFn, typename RemoveSelectedFn, typename CallArg>
	bool pop_next_scheduled_cc_candidate(BeginScanFn&& begin_scan, NextScanFn&& next_scan,
	                                     RemoveSelectedFn&& remove_selected, CallArg&& out_arg) {
		uint16_t selected_offset = 0;
		uint8_t selected_cc_number = 0;
		if (!select_scheduled_cc(begin_scan, next_scan, selected_offset, selected_cc_number)) {
			// No valid CC candidates were available to schedule.
			return false;
		}

		if (!remove_selected(selected_offset, std::forward<CallArg>(out_arg))) {
			// The transport could not remove/copy the selected message span.
			return false;
		}

		// Only advance scheduler state after the transport successfully removes the candidate.
		commit_scheduled_cc_pop(selected_cc_number);
		return true;
	}

	/// @brief Records that a CC number has work waiting or was coalesced in place.
	/// @param cc_number CC number to credit; ignored if out of range.
	void bump_cc_debt(uint8_t cc_number) {
		if (cc_number <= kMaxMIDIValue && cc_debt[cc_number] < k_max_cc_debt) {
			// Saturate instead of wrapping so a very hot CC stays preferred.
			cc_debt[cc_number]++;
		}
	}

	/// @brief Clears debt after a CC number's current value has actually been emitted.
	/// @param cc_number CC number to clear; ignored if out of range.
	void clear_cc_debt(uint8_t cc_number) {
		if (cc_number <= kMaxMIDIValue) {
			// Once the current value has left the queue, this CC no longer needs preference.
			cc_debt[cc_number] = 0;
		}
	}

private:
	static constexpr uint16_t k_no_cc_offset = 0xFFFF;
	static constexpr uint8_t k_max_cc_debt = std::numeric_limits<uint8_t>::max();

	/// @brief Marks CC numbers already represented by an earlier candidate during one selection pass.
	///
	/// @note A bitmask rather than a 128-entry map, so clearing it per pass is four stores, not 128.
	std::array<uint32_t, ((kMaxMIDIValue + 1) + 31) / 32> seen_mask{};

	/// @brief Returns whether this CC number was already seen this pass, and marks it seen.
	/// @param cc_number CC number to test and mark.
	/// @return True if @p cc_number was already seen this pass.
	bool test_and_set_seen(uint8_t cc_number) {
		uint32_t& word = seen_mask[cc_number >> 5];
		uint32_t bit = 1u << (cc_number & 31);
		bool already = (word & bit) != 0;
		word |= bit;
		return already;
	}
	/// @brief Saturating score used to prioritize CC numbers whose queued value changed.
	std::array<uint8_t, kMaxMIDIValue + 1> cc_debt{};
	/// @brief Round-robin starting point so equal-debt CC numbers share service.
	uint8_t next_cc_number{0};

	/// @brief Advances the round-robin starting point and clears the serviced CC number's debt.
	/// @param selected_cc_number CC number that was just serviced.
	void commit_scheduled_cc_pop(uint8_t selected_cc_number) {
		if (selected_cc_number <= kMaxMIDIValue) {
			// The selected CC has now been serviced.
			cc_debt[selected_cc_number] = 0;
		}
		// Resume the next scan after the serviced CC number.
		next_cc_number = static_cast<uint8_t>((selected_cc_number + 1) & kMaxMIDIValue);
	}
};

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
	/// @brief Reads a queued entry from one lane without removing it.
	/// @param lane           Priority lane index.
	/// @param logical_offset Logical offset from the lane head.
	/// @return The entry at @p logical_offset.
	[[nodiscard]] T read_at(uint8_t lane, uint16_t logical_offset) const { return lanes[lane].peek(logical_offset); }

	/// @brief Removes and returns one lane's head entry.
	/// @param lane Priority lane index.
	/// @param out  Out: set to the removed entry on success.
	/// @return True if an entry was popped; false if that lane was empty.
	bool pop_head(uint8_t lane, T& out) { return lanes[lane].pop(out); }
	/// @brief Appends one entry to one lane.
	/// @param lane  Priority lane index.
	/// @param value Entry to enqueue.
	/// @return True if the entry was stored; false if that lane is full.
	bool push(uint8_t lane, T value) { return lanes[lane].push(value); }
	/// @brief Overwrites a queued entry in one lane in place.
	/// @param lane           Priority lane index.
	/// @param logical_offset Logical offset from the lane head of the entry to overwrite.
	/// @param value          New value for that entry.
	void overwrite_at(uint8_t lane, uint16_t logical_offset, T value) {
		lanes[lane].overwrite_at(logical_offset, value);
	}
	/// @brief Removes a logical span from one lane by exchanging it with that lane's head.
	/// @param lane          Priority lane index.
	/// @param target_offset Logical offset of the span to remove, relative to the lane head.
	/// @param span          Width of the span, in entries.
	/// @param removed_out   Destination buffer for the removed entries.
	/// @return True if the span was removed; false if it falls outside the lane's current contents.
	bool remove_span_via_head_swap(uint8_t lane, uint16_t target_offset, uint16_t span, T* removed_out) {
		return lanes[lane].remove_span_via_head_swap(target_offset, span, removed_out);
	}
	/// @brief Returns whether one lane holds no queued entries.
	/// @param lane Priority lane index.
	/// @return True if that lane is empty.
	[[nodiscard]] bool empty(uint8_t lane) const { return lanes[lane].empty(); }
	/// @brief Returns how many more entries can be pushed to one lane before it is full.
	/// @param lane Priority lane index.
	/// @return Remaining usable capacity of that lane.
	[[nodiscard]] uint16_t space(uint8_t lane) const { return lanes[lane].space(); }

	/// @}

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

/// @brief Per-device queue storage plus CC coalescing/scheduling policy for one transport device.
///
/// Combines a MIDIQueueStorage of priority lanes with a MIDICCQueuePolicy layered on top. Most
/// methods here simply forward to one or the other; see those classes for full documentation.
template <typename T, size_t LaneCount, uint16_t const (&Capacities)[LaneCount]>
class MIDIQueueManagerDeviceState {
public:
	/// @copydoc MIDIQueueStorage::queue_count
	[[nodiscard]] uint16_t queue_count(uint8_t lane) const { return queue_storage.queue_count(lane); }
	/// @copydoc MIDIQueueStorage::total_queued_messages
	[[nodiscard]] uint32_t total_queued_messages() const { return queue_storage.total_queued_messages(); }
	/// @copydoc MIDIQueueStorage::head
	[[nodiscard]] T head(uint8_t lane) const { return queue_storage.head(lane); }
	/// @copydoc MIDIQueueStorage::read_at
	[[nodiscard]] T read_at(uint8_t lane, uint16_t logical_offset) const {
		return queue_storage.read_at(lane, logical_offset);
	}
	/// @copydoc MIDIQueueStorage::pop_head
	bool pop_head(uint8_t lane, T& out) { return queue_storage.pop_head(lane, out); }
	/// @copydoc MIDIQueueStorage::push
	bool push(uint8_t lane, T value) { return queue_storage.push(lane, value); }
	/// @copydoc MIDIQueueStorage::overwrite_at
	void overwrite_at(uint8_t lane, uint16_t logical_offset, T value) {
		queue_storage.overwrite_at(lane, logical_offset, value);
	}
	/// @copydoc MIDIQueueStorage::remove_span_via_head_swap
	bool remove_span_via_head_swap(uint8_t lane, uint16_t target_offset, uint16_t span, T* removed_out) {
		return queue_storage.remove_span_via_head_swap(lane, target_offset, span, removed_out);
	}
	/// @copydoc MIDIQueueStorage::empty
	[[nodiscard]] bool empty(uint8_t lane) const { return queue_storage.empty(lane); }
	/// @copydoc MIDIQueueStorage::space
	[[nodiscard]] uint16_t space(uint8_t lane) const { return queue_storage.space(lane); }
	/// @copydoc MIDIQueueStorage::lane_capacity
	[[nodiscard]] uint16_t lane_capacity(uint8_t lane) const { return queue_storage.lane_capacity(lane); }
	/// @brief Returns whether any lane on this device has queued entries.
	/// @return True if at least one lane has a queued entry.
	[[nodiscard]] bool has_any_data() const { return queue_storage.total_queued_messages() > 0; }
	/// @brief Clears all lanes plus the CC scheduling/coalescing bookkeeping.
	void clear_all() {
		for (auto& queue_lane : queue_storage.lanes) {
			// Drop queued transport data from every priority lane.
			queue_lane.clear();
		}
		// Reset scheduler state so stale debt/scan data does not survive a device reset.
		cc_queue_policy.reset();
	}
	/// @copydoc MIDIQueueLane::pop_many
	bool pop_many(uint8_t lane, T* out, uint16_t count) { return queue_storage.lanes[lane].pop_many(out, count); }

	/// @copydoc MIDICCQueuePolicy::coalesce_latest_matching_cc
	template <typename BeginScanFn, typename NextScanFn, typename UpdateMatchedFn>
	bool coalesce_latest_matching_cc(uint8_t wanted_status, uint8_t wanted_cc_number, BeginScanFn&& begin_scan,
	                                 NextScanFn&& next_scan, UpdateMatchedFn&& update_matched) {
		return cc_queue_policy.coalesce_latest_matching_cc(
		    wanted_status, wanted_cc_number, std::forward<BeginScanFn>(begin_scan), std::forward<NextScanFn>(next_scan),
		    std::forward<UpdateMatchedFn>(update_matched));
	}

	/// @copydoc MIDICCQueuePolicy::pop_next_scheduled_cc_candidate
	template <typename BeginScanFn, typename NextScanFn, typename RemoveSelectedFn, typename CallArg>
	bool pop_next_scheduled_cc_candidate(BeginScanFn&& begin_scan, NextScanFn&& next_scan,
	                                     RemoveSelectedFn&& remove_selected, CallArg&& out_arg) {
		return cc_queue_policy.pop_next_scheduled_cc_candidate(
		    std::forward<BeginScanFn>(begin_scan), std::forward<NextScanFn>(next_scan),
		    std::forward<RemoveSelectedFn>(remove_selected), std::forward<CallArg>(out_arg));
	}

	/// @copydoc MIDICCQueuePolicy::bump_cc_debt
	void bump_cc_debt(uint8_t cc_number) { cc_queue_policy.bump_cc_debt(cc_number); }
	/// @copydoc MIDICCQueuePolicy::clear_cc_debt
	void clear_cc_debt(uint8_t cc_number) { cc_queue_policy.clear_cc_debt(cc_number); }

private:
	MIDIQueueStorage<T, LaneCount, Capacities> queue_storage{};
	/// @brief Per-device CC coalescing/scheduling policy layered on top of transport queue storage.
	MIDICCQueuePolicy cc_queue_policy{};
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
	void reset_queue_storage();
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
	MIDIQueueManagerDeviceState<uint32_t, QUEUE_PRIORITY_COUNT, k_usb_lane_capacity> queue_manager_{};

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

	/// @brief Starts scanning the USB CC lane, where each logical entry is one queued message.
	/// @param scan_position Out: scan cursor initialized to the start of the lane.
	/// @param limit         Out: scan cursor's upper bound.
	/// @return True if the CC lane has entries to scan.
	[[nodiscard]] bool begin_cc_message_scan(uint16_t& scan_position, uint16_t& limit) const;
	/// @brief Reads the next USB CC message scan entry shared by scheduled dequeue and coalescing scans.
	/// @param scan_position Scan cursor; advanced past the returned entry.
	/// @param limit         Scan cursor's upper bound.
	/// @param message       Out: the scanned message, valid when the result is Found.
	/// @return The scan step outcome.
	[[nodiscard]] MIDIQueueManager::CCMessageScanResult
	next_cc_message(uint16_t& scan_position, uint16_t limit, MIDIQueueManager::CCMessageScanEntry& message) const;
	/// @brief Removes one selected CC message from the USB queue lane.
	/// @param target_offset Logical offset of the message to remove.
	/// @param popped_out    Out: the removed, packed USB-MIDI event.
	/// @return True if the message was removed.
	[[nodiscard]] bool remove_cc_message_at(uint16_t target_offset, uint32_t& popped_out);
	/// @brief Pops the next scheduled CC message, possibly from deeper than the CC lane head.
	/// @param message_out Out: the popped, packed USB-MIDI event.
	/// @return True if a scheduled CC message was popped.
	[[nodiscard]] bool pop_next_scheduled_cc_message(uint32_t& message_out);
	/// @brief Replaces a queued matching CC message's value byte with the newest value.
	/// @param queued_message Packed USB-MIDI event carrying the newest value.
	/// @return True if a match was found and updated.
	[[nodiscard]] bool coalesce_cc_message(uint32_t queued_message);
	/// @brief Appends one packed USB-MIDI event to its priority lane.
	/// @param priority       Target priority lane.
	/// @param queued_message Packed USB-MIDI event to enqueue.
	/// @return True if the event was stored; false if that lane is full.
	[[nodiscard]] bool enqueue_priority_message(QueuePriority priority, uint32_t queued_message);
	/// @brief Applies USB enqueue-time CC coalescing/debt policy without callback glue.
	/// @param priority       Target priority lane.
	/// @param queued_message Packed USB-MIDI event to enqueue or coalesce.
	/// @return True if the event was coalesced into an existing entry or newly enqueued.
	[[nodiscard]] bool enqueue_message_with_cc_policy(QueuePriority priority, uint32_t queued_message);
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
	void reset_queue_storage();
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
	MIDIQueueManagerDeviceState<uint8_t, k_serial_priority_count, k_din_lane_capacity> queue_manager_{};
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

	/// @brief Starts scanning the DIN CC lane, where a logical message spans 1-3 bytes.
	/// @param scan_position Out: scan cursor initialized to the start of the lane.
	/// @param limit         Out: scan cursor's upper bound.
	/// @return True if the CC lane has entries to scan.
	[[nodiscard]] bool begin_cc_message_scan(uint16_t& scan_position, uint16_t& limit) const;
	/// @brief Reads the next complete DIN CC message scan entry shared by scheduled dequeue and coalescing scans.
	/// @param scan_position Scan cursor; advanced past the returned entry.
	/// @param limit         Scan cursor's upper bound.
	/// @param message       Out: the scanned message, valid when the result is Found.
	/// @return The scan step outcome.
	[[nodiscard]] MIDIQueueManager::CCMessageScanResult
	next_cc_message(uint16_t& scan_position, uint16_t limit, MIDIQueueManager::CCMessageScanEntry& message) const;
	/// @brief Removes one selected three-byte CC message from the byte queue.
	/// @param target_offset Logical offset of the message to remove.
	/// @param out           Destination buffer for the removed 3 bytes.
	/// @return True if the message was removed.
	[[nodiscard]] bool remove_cc_message_at(uint16_t target_offset, uint8_t* out);
	/// @brief Pops the next scheduled complete CC message while respecting DIN allowance and UART space.
	/// @param out_bytes       Destination buffer for the popped message bytes.
	/// @param allowance_bytes Remaining caller send allowance, in bytes.
	/// @param uart_space      Remaining space in the UART output buffer.
	/// @param max_len         Caller-imposed maximum message length for this pop.
	/// @param popped_priority Out: priority lane the popped message came from.
	/// @return True if a scheduled CC message was popped.
	[[nodiscard]] bool pop_next_scheduled_cc_message(uint8_t* out_bytes, int32_t allowance_bytes, int32_t uart_space,
	                                                 int32_t max_len, QueuePriority& popped_priority);
	/// @brief Replaces the queued value byte for the latest matching status/CC number.
	/// @param queued_message Message carrying the newest value.
	/// @return True if a match was found and updated.
	[[nodiscard]] bool coalesce_cc_message(MIDIMessage queued_message);
	/// @brief Encodes one MIDIMessage to serial bytes and appends it to a priority lane.
	/// @param priority       Target priority lane.
	/// @param queued_message Message to enqueue.
	/// @return True if the message was stored; false if that lane is full.
	[[nodiscard]] bool enqueue_priority_message(QueuePriority priority, MIDIMessage queued_message);
	/// @brief Applies DIN enqueue-time CC coalescing/debt policy without callback glue.
	/// @param priority       Target priority lane.
	/// @param queued_message Message to enqueue or coalesce.
	/// @return True if the message was coalesced into an existing entry or newly enqueued.
	[[nodiscard]] bool enqueue_message_with_cc_policy(QueuePriority priority, MIDIMessage queued_message);
	/// @brief Decides how to advance CC lane traversal during a DIN dequeue pass.
	/// @param priority Priority lane under consideration.
	/// @param context  Output destination, pacing allowances, and popped-lane result.
	/// @return How the caller should proceed for this lane.
	[[nodiscard]] MIDIQueueManager::PriorityLaneTraversalResult handle_cc_lane(QueuePriority priority,
	                                                                           DINSendContext& context);
};
