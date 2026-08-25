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

template <typename T, uint16_t Capacity, size_t LaneCount>
class MIDIQueueManagerDeviceState;

/// Shared MIDI queue policy helpers used across transport-specific queue managers.
///
/// The transport managers below store different queue units: USB stores one
/// packed USB-MIDI event per queue entry, while DIN stores raw serial bytes.
/// This class contains only the policy decisions that are independent of that
/// storage format: message classification, CC detection, and common gate checks.
class MIDIQueueManager {
public:
	static constexpr uint8_t k_channel_cc_status_nibble = 0x0B;
	static constexpr int32_t k_channel_cc_message_length = 3;

	/// Returns true for channel-CC status bytes (0xBn).
	static constexpr bool is_channel_cc_status_byte(uint8_t status) {
		return (status >> 4) == k_channel_cc_status_nibble;
	}

	/// Returns true for channel-CC status-type nibbles.
	static constexpr bool is_channel_cc_status_type(uint8_t status_type) {
		return status_type == k_channel_cc_status_nibble;
	}

	/// Returns true only for full 3-byte channel-CC messages.
	static constexpr bool is_three_byte_channel_cc(uint8_t status, int32_t message_len) {
		return message_len == k_channel_cc_message_length && is_channel_cc_status_byte(status);
	}

	enum class CCMessageScanResult : uint8_t {
		NoMore,
		Skip,
		Found,
		Invalid,
	};

	struct CCMessageScanEntry {
		uint16_t offset;
		uint8_t status;
		uint8_t cc_number;
	};

	enum class CandidateScanResult : uint8_t {
		NoMore,
		Skip,
		Candidate,
		Invalid,
	};

	enum class CoalesceScanResult : uint8_t {
		NoMore,
		Skip,
		Matchable,
		Invalid,
	};

	enum class CCScheduledPopResult : uint8_t {
		NotCC,
		AllowanceBlocked,
		PopFailed,
		Popped,
	};

	enum class PriorityLaneTraversalResult {
		PopLane,
		SkipLane,
		Popped,
		Abort,
	};

	enum class HeadMessageCheckResult : uint8_t {
		Invalid,
		InsufficientCapacity,
		Ready,
	};

	/// Classifies an outgoing MIDI message into priority groups.
	///
	/// Defined inline rather than in the .cpp so it can be unit tested without the UART and USB layers.
	/// Intent is consumed here and nowhere else: routing Event CCs away from the CC lane leaves that
	/// lane holding only Continuous entries, which is what lets the dequeue path coalesce and reorder
	/// unconditionally without needing to know any entry's intent (there is nowhere to store it - a USB
	/// entry is a fully packed uint32 and a DIN entry is a raw byte).
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

	/// Converts a transport CC scan entry into the candidate shape used by scheduled dequeue.
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

	/// Converts a transport CC scan entry into the match shape used by CC coalescing.
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

	/// Shared CC gate helper: if head is CC and allowance permits, attempt a scheduled pop.
	///
	/// A transport asks this before popping the CC lane head. Non-CC messages
	/// fall through to normal lane-order popping. CC messages only use the
	/// scheduler when the caller's per-transfer/per-UART allowance still permits it.
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

	/// Shared parser+fit gate for queued non-realtime MIDI messages.
	///
	/// Returns Ready only when the queue head decodes to a valid message length
	/// and that complete message fits queue occupancy plus all caller limits.
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

/// Stateful CC coalescing and scheduling policy instantiated per transport/device.
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
	MIDICCQueuePolicy() { reset(); }

	/// Clears scan scratch state, CC debt, and CC-number selection history.
	void reset() {
		// Drop any pending preference accumulated by queued/coalesced CC updates.
		cc_debt.fill(0);
		// Restart round-robin selection from CC 0.
		next_cc_number = 0;
	}

	/// Chooses the next CC to emit in a single pass over the lane.
	///
	/// Selection follows the queued order for each CC number (the first entry for a CC number wins) and
	/// then prefers the CC with the most debt, falling back to round-robin from `next_cc_number`.
	///
	/// This deliberately does the whole job in one traversal. The previous shape filled a 128-entry offset
	/// map, scanned the lane, then walked all 128 CC numbers to pick a winner - per pop, up to eight times
	/// per transfer, inside the interrupt-masked section of flushUSBMIDIOutput(). Folding selection into
	/// the scan drops that to O(lane occupancy) with a four-word bitmask clear, and costs nothing extra
	/// when the lane is short, which is the common case once coalescing has collapsed repeated CC updates.
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

	/// Finds the newest queued CC matching a status/CC number pair.
	///
	/// Enqueue-time coalescing intentionally updates the latest match, preserving
	/// the queued order while ensuring the eventual send uses the freshest value.
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

	/// Finds a matching queued CC, lets the transport update it, then records CC debt.
	///
	/// USB and DIN store different queue units, so the policy does not know how
	/// to rewrite the queued value byte. The transport callback receives the
	/// logical offset of the latest matching CC and performs that overwrite.
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

	/// Selects and removes the next scheduled CC candidate from a transport-specific lane.
	///
	/// The lane scan discovers candidates, the policy chooses the CC number,
	/// and the transport callback removes the selected queue span.
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

	/// Records that a CC number has work waiting or was coalesced in place.
	void bump_cc_debt(uint8_t cc_number) {
		if (cc_number <= kMaxMIDIValue && cc_debt[cc_number] < k_max_cc_debt) {
			// Saturate instead of wrapping so a very hot CC stays preferred.
			cc_debt[cc_number]++;
		}
	}

	/// Clears debt after a CC number's current value has actually been emitted.
	void clear_cc_debt(uint8_t cc_number) {
		if (cc_number <= kMaxMIDIValue) {
			// Once the current value has left the queue, this CC no longer needs preference.
			cc_debt[cc_number] = 0;
		}
	}

private:
	static constexpr uint16_t k_no_cc_offset = 0xFFFF;
	static constexpr uint8_t k_max_cc_debt = std::numeric_limits<uint8_t>::max();

	/// Marks CC numbers already represented by an earlier candidate during one selection pass.
	/// A bitmask rather than a 128-entry map so clearing it per pass is four stores, not 128.
	std::array<uint32_t, ((kMaxMIDIValue + 1) + 31) / 32> seen_mask{};

	/// Returns whether this CC number was already seen this pass, and marks it seen.
	bool test_and_set_seen(uint8_t cc_number) {
		uint32_t& word = seen_mask[cc_number >> 5];
		uint32_t bit = 1u << (cc_number & 31);
		bool already = (word & bit) != 0;
		word |= bit;
		return already;
	}
	/// Saturating score used to prioritize CC numbers whose queued value changed.
	std::array<uint8_t, kMaxMIDIValue + 1> cc_debt{};
	/// Round-robin starting point so equal-debt CC numbers share service.
	uint8_t next_cc_number{0};

	/// Advances the round-robin starting point and clears the serviced CC number's debt.
	void commit_scheduled_cc_pop(uint8_t selected_cc_number) {
		if (selected_cc_number <= kMaxMIDIValue) {
			// The selected CC has now been serviced.
			cc_debt[selected_cc_number] = 0;
		}
		// Resume the next scan after the serviced CC number.
		next_cc_number = static_cast<uint8_t>((selected_cc_number + 1) & kMaxMIDIValue);
	}
};

/// Power-of-two ring buffer lane shared by transport-specific queue managers.
///
/// The lane keeps one slot unused so `read_pos == write_pos` can represent
/// empty. Therefore, usable capacity is `Capacity - 1`. All logical offsets are
/// relative to `read_pos`, which lets callers scan or overwrite queued messages
/// without exposing wrapped physical indices.
template <typename T, uint16_t Capacity>
class MIDIQueueLane {
public:
	static_assert(Capacity != 0);
	static_assert((Capacity & (Capacity - 1)) == 0);
	static constexpr uint16_t k_capacity = Capacity;

	std::array<T, Capacity> data{};
	uint16_t read_pos{0};
	uint16_t write_pos{0};

	[[nodiscard]] bool empty() const { return read_pos == write_pos; }
	[[nodiscard]] uint16_t size() const { return static_cast<uint16_t>((write_pos - read_pos) & (Capacity - 1)); }
	[[nodiscard]] uint16_t space() const { return static_cast<uint16_t>((Capacity - 1) - size()); }
	[[nodiscard]] T peek(uint16_t offset = 0) const { return data[(read_pos + offset) & (Capacity - 1)]; }

	bool push(T value) {
		// Advance write position first so we can detect the one-unused-slot full case.
		uint16_t next = static_cast<uint16_t>((write_pos + 1) & (Capacity - 1));
		if (next == read_pos) {
			return false;
		}
		// Store into the current write slot, then publish it by moving write_pos.
		data[write_pos] = value;
		write_pos = next;
		return true;
	}

	bool pop(T& out) {
		if (empty()) {
			return false;
		}
		// Copy the oldest queued entry before advancing the read position.
		out = data[read_pos];
		read_pos = static_cast<uint16_t>((read_pos + 1) & (Capacity - 1));
		return true;
	}

	bool pop_many(T* out, uint16_t count) {
		if (size() < count) {
			// The caller asked for an atomic span; do not partially pop it.
			return false;
		}
		// Copy the contiguous logical span, even if the physical ring wraps.
		for (uint16_t i = 0; i < count; i++) {
			out[i] = data[(read_pos + i) & (Capacity - 1)];
		}
		// Commit the pop only after all requested entries were copied.
		read_pos = static_cast<uint16_t>((read_pos + count) & (Capacity - 1));
		return true;
	}

	/// Removes a logical span by exchanging it with the head, then dropping the head.
	///
	/// Scheduled CC dequeue can select an entry that is not at the lane head. Two properties matter here:
	///
	///  - `write_pos` is owned by the producer. `consume_queued_messages()` runs in an ISR (see the
	///    warnings in midi_engine.cpp), so a consumer that cleared and rebuilt the ring - as the earlier
	///    repack did - would race a mainline `push()` and could lose or reorder queued MIDI. This touches
	///    only `read_pos` and the two spans being exchanged.
	///  - The freed slot is reclaimed immediately. Marking the span dead in place instead would leak one
	///    slot per out-of-order removal until it reached the head, which under sustained CC automation
	///    fills the lane and starts dropping messages.
	///
	/// Scheduled removal only runs when the head is itself a three-byte channel CC, so the head span and
	/// the selected span are always the same width. The entry displaced from the head moves to the
	/// selected position; CC entries are independent of one another, and this feature already reorders
	/// them by design, so that is within the ordering the scheduler is allowed to produce.
	bool remove_span_via_head_swap(uint16_t target_offset, uint16_t span, T* removed_out) {
		uint16_t queue_size = size();
		if (span > queue_size || target_offset > static_cast<uint16_t>(queue_size - span)) {
			// The requested span is outside the current logical queue contents.
			return false;
		}

		for (uint16_t i = 0; i < span; i++) {
			uint16_t head_index = static_cast<uint16_t>((read_pos + i) & (Capacity - 1));
			uint16_t selected_index = static_cast<uint16_t>((read_pos + target_offset + i) & (Capacity - 1));
			// Copy the selected entry out, then move the head entry into the slot it vacated.
			removed_out[i] = data[selected_index];
			data[selected_index] = data[head_index];
		}

		// Dropping the head is what actually frees the slot. When target_offset is 0 this degenerates to a
		// plain pop, which is exactly right.
		read_pos = static_cast<uint16_t>((read_pos + span) & (Capacity - 1));
		return true;
	}

	void clear() {
		// With this ring representation, equal positions mean empty.
		read_pos = 0;
		write_pos = 0;
	}

	void overwrite_at(uint16_t logical_offset, T value) { data[(read_pos + logical_offset) & (Capacity - 1)] = value; }
};

/// Fixed set of power-of-two queue lanes shared by a transport-specific manager.
template <typename T, uint16_t Capacity, size_t LaneCount>
class MIDIQueueStorage {
public:
	std::array<MIDIQueueLane<T, Capacity>, LaneCount> lanes{};

	[[nodiscard]] uint16_t queue_count(uint8_t lane) const { return lanes[lane].size(); }
	[[nodiscard]] uint32_t total_queued_messages() const {
		uint32_t queued = 0;
		for (auto const& queue_lane : lanes) {
			// Sum all priority lanes so callers can make whole-device decisions.
			queued += queue_lane.size();
		}
		return queued;
	}

	[[nodiscard]] T head(uint8_t lane) const { return lanes[lane].peek(); }
	[[nodiscard]] T read_at(uint8_t lane, uint16_t logical_offset) const { return lanes[lane].peek(logical_offset); }

	bool pop_head(uint8_t lane, T& out) { return lanes[lane].pop(out); }
	bool push(uint8_t lane, T value) { return lanes[lane].push(value); }
	void overwrite_at(uint8_t lane, uint16_t logical_offset, T value) {
		lanes[lane].overwrite_at(logical_offset, value);
	}
	bool remove_span_via_head_swap(uint8_t lane, uint16_t target_offset, uint16_t span, T* removed_out) {
		return lanes[lane].remove_span_via_head_swap(target_offset, span, removed_out);
	}
	[[nodiscard]] bool empty(uint8_t lane) const { return lanes[lane].empty(); }
	[[nodiscard]] uint16_t space(uint8_t lane) const { return lanes[lane].space(); }
};

template <typename T, uint16_t Capacity, size_t LaneCount>
class MIDIQueueManagerDeviceState {
public:
	[[nodiscard]] uint16_t queue_count(uint8_t lane) const { return queue_storage.queue_count(lane); }
	[[nodiscard]] uint32_t total_queued_messages() const { return queue_storage.total_queued_messages(); }
	[[nodiscard]] T head(uint8_t lane) const { return queue_storage.head(lane); }
	[[nodiscard]] T read_at(uint8_t lane, uint16_t logical_offset) const {
		return queue_storage.read_at(lane, logical_offset);
	}
	bool pop_head(uint8_t lane, T& out) { return queue_storage.pop_head(lane, out); }
	bool push(uint8_t lane, T value) { return queue_storage.push(lane, value); }
	void overwrite_at(uint8_t lane, uint16_t logical_offset, T value) {
		queue_storage.overwrite_at(lane, logical_offset, value);
	}
	bool remove_span_via_head_swap(uint8_t lane, uint16_t target_offset, uint16_t span, T* removed_out) {
		return queue_storage.remove_span_via_head_swap(lane, target_offset, span, removed_out);
	}
	[[nodiscard]] bool empty(uint8_t lane) const { return queue_storage.empty(lane); }
	[[nodiscard]] uint16_t space(uint8_t lane) const { return queue_storage.space(lane); }
	[[nodiscard]] bool has_any_data() const { return queue_storage.total_queued_messages() > 0; }
	/// Clears all lanes plus the CC scheduling/coalescing bookkeeping.
	void clear_all() {
		for (auto& queue_lane : queue_storage.lanes) {
			// Drop queued transport data from every priority lane.
			queue_lane.clear();
		}
		// Reset scheduler state so stale debt/scan data does not survive a device reset.
		cc_queue_policy.reset();
	}
	bool pop_many(uint8_t lane, T* out, uint16_t count) { return queue_storage.lanes[lane].pop_many(out, count); }

	template <typename BeginScanFn, typename NextScanFn, typename UpdateMatchedFn>
	bool coalesce_latest_matching_cc(uint8_t wanted_status, uint8_t wanted_cc_number, BeginScanFn&& begin_scan,
	                                 NextScanFn&& next_scan, UpdateMatchedFn&& update_matched) {
		return cc_queue_policy.coalesce_latest_matching_cc(
		    wanted_status, wanted_cc_number, std::forward<BeginScanFn>(begin_scan), std::forward<NextScanFn>(next_scan),
		    std::forward<UpdateMatchedFn>(update_matched));
	}

	template <typename BeginScanFn, typename NextScanFn, typename RemoveSelectedFn, typename CallArg>
	bool pop_next_scheduled_cc_candidate(BeginScanFn&& begin_scan, NextScanFn&& next_scan,
	                                     RemoveSelectedFn&& remove_selected, CallArg&& out_arg) {
		return cc_queue_policy.pop_next_scheduled_cc_candidate(
		    std::forward<BeginScanFn>(begin_scan), std::forward<NextScanFn>(next_scan),
		    std::forward<RemoveSelectedFn>(remove_selected), std::forward<CallArg>(out_arg));
	}

	void bump_cc_debt(uint8_t cc_number) { cc_queue_policy.bump_cc_debt(cc_number); }
	void clear_cc_debt(uint8_t cc_number) { cc_queue_policy.clear_cc_debt(cc_number); }

private:
	MIDIQueueStorage<T, Capacity, LaneCount> queue_storage{};
	/// Per-device CC coalescing/scheduling policy layered on top of transport queue storage.
	MIDICCQueuePolicy cc_queue_policy{};
};

class MIDIQueueManagerUSB {
public:
	/// Clears USB queue contents and CC scheduling bookkeeping for this device.
	void reset_queue_storage();
	[[nodiscard]] bool has_buffered_send_data() const;
	[[nodiscard]] int send_buffer_space() const;
	void enqueue_message(uint32_t full_message);
	bool consume_queued_messages(uint8_t* data_sending_now, uint8_t& num_bytes_sending_now, bool usb_host_mode);

	struct USBSendContext {
		uint32_t& message_out;
		int32_t& cc_allowance_messages_remaining;
	};

private:
	/// Per-priority USB output queues. Each lane is a ring of packed USB-MIDI events;
	/// consume_queued_messages() drains them into dataSendingNow in priority order.
	MIDIQueueManagerDeviceState<uint32_t, MIDI_SEND_BUFFER_LEN_RING, QUEUE_PRIORITY_COUNT> queue_manager_{};

	/// Classifies an outgoing MIDI message into priority groups.
	[[nodiscard]] static QueuePriority classify_packed_usb_priority(uint32_t packed);
	/// Pops one queued message according to strict USB priority ordering.
	bool pop_lane(QueuePriority priority, USBSendContext& context);

	/// Starts scanning the USB CC lane, where each logical entry is one queued message.
	[[nodiscard]] bool begin_cc_message_scan(uint16_t& scan_position, uint16_t& limit) const;
	/// Reads the next USB CC message scan entry shared by scheduled dequeue and coalescing scans.
	[[nodiscard]] MIDIQueueManager::CCMessageScanResult
	next_cc_message(uint16_t& scan_position, uint16_t limit, MIDIQueueManager::CCMessageScanEntry& message) const;
	/// Removes one selected CC message from the USB queue lane.
	[[nodiscard]] bool remove_cc_message_at(uint16_t target_offset, uint32_t& popped_out);
	/// Pops the next scheduled CC message, possibly from deeper than the CC lane head.
	[[nodiscard]] bool pop_next_scheduled_cc_message(uint32_t& message_out);
	/// Replaces a queued matching CC message's value byte with the newest value.
	[[nodiscard]] bool coalesce_cc_message(uint32_t queued_message);
	/// Appends one packed USB-MIDI event to its priority lane.
	[[nodiscard]] bool enqueue_priority_message(QueuePriority priority, uint32_t queued_message);
	/// Applies USB enqueue-time CC coalescing/debt policy without callback glue.
	[[nodiscard]] bool enqueue_message_with_cc_policy(QueuePriority priority, uint32_t queued_message);
	[[nodiscard]] MIDIQueueManager::PriorityLaneTraversalResult handle_cc_lane(QueuePriority priority,
	                                                                           USBSendContext& context);
	/// Pops one queued SysEx event and keeps USB drain locked to SysEx until the ending event is sent.
	[[nodiscard]] bool pop_sysex_message(USBSendContext& context);
	/// True after a USB SysEx start/continue event has been popped but before its terminating event has been sent.
	bool sysex_drain_active_{false};
};

class MIDIQueueManagerDIN {
public:
	/// Clears DIN queue contents and CC scheduling bookkeeping for this device.
	void reset_queue_storage();
	/// Resets serial queue pacing state to a known baseline.
	void reset_serial_state(uint32_t now_sample_timer);
	/// Returns whether any serial-priority lane has pending bytes.
	[[nodiscard]] bool has_serial_data() const;
	/// Remaining DIN queue capacity for raw SysEx bytes.
	[[nodiscard]] size_t send_buffer_space() const;
	/// Queues one channel/system MIDI message into the serial-priority queues.
	void enqueue_message(MIDIMessage message);
	/// Queues one complete SysEx byte stream into the serial-priority queues.
	bool enqueue_sysex(uint8_t const* data, int32_t len);
	/// Drains serial-priority queues into UART under pacing and priority rules.
	void consume_queued_messages(uint32_t now_sample_timer);

	struct DINSendContext {
		uint8_t* out_bytes;
		int32_t allowance_bytes;
		int32_t uart_space;
		int32_t max_len;
		int32_t cc_uart_allowance;
		QueuePriority& popped_priority;
	};

private:
	/// Number of active serial-priority lanes [clock..SysEx] scanned during dequeue.
	static constexpr size_t k_serial_priority_count = QUEUE_PRIORITY_COUNT;
	/// Power-of-two lane capacity. Larger than MIDI_TX_BUFFER_SIZE so a full
	/// 1024-byte SysEx can fit despite the ring's one-unused-slot invariant.
	static constexpr uint16_t k_serial_queue_capacity = MIDI_TX_BUFFER_SIZE * 2;
	/// Per-priority byte rings holding pending DIN output grouped by queue policy.
	MIDIQueueManagerDeviceState<uint8_t, k_serial_queue_capacity, k_serial_priority_count> queue_manager_{};
	/// Last sample-timer tick used to accrue DIN send allowance.
	uint32_t serial_allowance_last_update_{0};
	/// Accumulated DIN send allowance in Q8 bytes (8 fractional bits).
	int32_t serial_allowance_Q8_{0};
	/// True after DIN begins draining a SysEx byte stream and before 0xF7 is sent.
	bool sysex_drain_active_{false};

	/// Pops one realtime/system byte or one complete MIDI message according to lane priority.
	bool pop_lane(QueuePriority priority, DINSendContext& context);
	/// Pops one queued SysEx byte and keeps DIN drain locked to SysEx until 0xF7 is sent.
	bool pop_sysex_byte(DINSendContext& context);

	/// Starts scanning the DIN CC lane, where a logical message spans 1-3 bytes.
	[[nodiscard]] bool begin_cc_message_scan(uint16_t& scan_position, uint16_t& limit) const;
	/// Reads the next complete DIN CC message scan entry shared by scheduled dequeue and coalescing scans.
	[[nodiscard]] MIDIQueueManager::CCMessageScanResult
	next_cc_message(uint16_t& scan_position, uint16_t limit, MIDIQueueManager::CCMessageScanEntry& message) const;
	/// Removes one selected three-byte CC message from the byte queue.
	[[nodiscard]] bool remove_cc_message_at(uint16_t target_offset, uint8_t* out);
	/// Pops the next scheduled complete CC message while respecting DIN allowance and UART space.
	[[nodiscard]] bool pop_next_scheduled_cc_message(uint8_t* out_bytes, int32_t allowance_bytes, int32_t uart_space,
	                                                 int32_t max_len, QueuePriority& popped_priority);
	/// Replaces the queued value byte for the latest matching status/CC number.
	[[nodiscard]] bool coalesce_cc_message(MIDIMessage queued_message);
	/// Encodes one MIDIMessage to serial bytes and appends it to a priority lane.
	[[nodiscard]] bool enqueue_priority_message(QueuePriority priority, MIDIMessage queued_message);
	/// Applies DIN enqueue-time CC coalescing/debt policy without callback glue.
	[[nodiscard]] bool enqueue_message_with_cc_policy(QueuePriority priority, MIDIMessage queued_message);
	[[nodiscard]] MIDIQueueManager::PriorityLaneTraversalResult handle_cc_lane(QueuePriority priority,
	                                                                           DINSendContext& context);
};
