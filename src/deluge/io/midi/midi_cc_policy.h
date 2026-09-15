/*
 * Copyright © 2026 Katherine Whitlock
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
#include "io/midi/midi_queue_policy.h"
#include "timers_interrupts/timers_interrupts.h"
#include <array>
#include <cstdint>
#include <limits>
#include <utility>

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
	/// @note Fuses the scan and the winner selection into a single traversal - O(lane occupancy) with a
	///       four-word seen_mask clear - rather than building a candidate map and then walking all 128 CC
	///       numbers to pick a winner. This runs inside the interrupt-masked section of
	///       flushUSBMIDIOutput(), and costs nothing extra when the lane is short, which is the common
	///       case once coalescing has collapsed repeated CC updates.
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

			// Rank orders CC numbers starting at next_cc_number and wrapping, so ties resolve in
			// round-robin order.
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

/// @brief The CC-lane half of the queue policy, written once and shared by both transports.
///
/// Everything that genuinely differs between USB and DIN is in the Transport traits: element type, how
/// identity is read out of an element, how many elements one message spans, and how a value byte is
/// rewritten.
///
/// @note Stateless. All queue state lives in the lane it is handed, and all scheduling state lives in
///       the MIDICCQueuePolicy it is handed, so one instance per manager costs nothing.
/// @note Only the *CC-policy* interaction is shared. The drain loops stay transport-specific: USB fills
///       a fixed-size transfer against a message-count allowance, DIN paces bytes against a Q8 allowance
///       and UART space. Those are different algorithms, not one algorithm with different constants.
template <typename Transport>
class MIDICCLanePolicy {
public:
	using Element = typename Transport::Element;

	/// @brief Rewrites the value of the newest queued CC matching @p status and @p cc_number.
	/// @param lane      CC lane to search and update.
	/// @param status    Status byte (type and channel) to match.
	/// @param cc_number CC number to match.
	/// @param value     New value byte to write into the matched message.
	/// @param debt      Scheduling policy credited when a match is rewritten.
	/// @return True if a match was found and rewritten; false if the caller should append instead.
	template <typename Lane>
	bool coalesce(Lane& lane, uint8_t status, uint8_t cc_number, uint8_t value, MIDICCQueuePolicy& debt);

	/// @brief Selects and removes the next CC to emit, by debt then round-robin.
	/// @param lane CC lane to select from and remove out of.
	/// @param debt Scheduling policy that owns selection order and is advanced on success.
	/// @param out  Destination for the removed message; must hold at least Transport::cc_span elements.
	/// @return True if a CC was selected and copied into @p out.
	template <typename Lane>
	bool pop_scheduled(Lane& lane, MIDICCQueuePolicy& debt, Element* out);

	/// @brief Applies enqueue-time CC coalescing and debt policy around a transport's append step.
	///
	/// A Continuous CC whose identity is already queued replaces that queued value instead of adding a
	/// second message; anything else is appended by @p append, which is the only transport-specific part
	/// (USB pushes one packed event, DIN encodes and pushes one to three raw bytes).
	///
	/// @param cc_lane       The CC lane, which is the only lane coalescing may touch.
	/// @param debt          Scheduling policy credited when a CC is coalesced or newly queued.
	/// @param to_cc_lane    True when this message classified into the CC lane.
	/// @param is_channel_cc True when this message is a channel CC (0xBn).
	/// @param status        Status byte of the message being enqueued.
	/// @param cc_number     CC number of the message being enqueued.
	/// @param value         Value byte of the message being enqueued.
	/// @param append        Transport callback that appends the message to its lane; returns success.
	/// @return True if the message was coalesced into an existing entry or newly appended.
	template <typename Lane, typename AppendFn>
	bool enqueue_with_cc_policy(Lane& cc_lane, MIDICCQueuePolicy& debt, bool to_cc_lane, bool is_channel_cc,
	                            uint8_t status, uint8_t cc_number, uint8_t value, AppendFn&& append);

private:
	/// @brief Steps to the next queued channel CC, honouring the transport's span.
	///
	/// @param lane      CC lane to read.
	/// @param offset    In/out: logical offset to read from; advanced past the message on success, so the
	///                  message just reported starts at `offset - Transport::cc_span`.
	/// @param status    Out: status byte of the message, set only on success.
	/// @param cc_number Out: CC number of the message, set only on success.
	/// @return True if a channel CC was read; false at the end of the lane or on a misaligned lane.
	template <typename Lane>
	static bool next_cc(Lane const& lane, uint16_t& offset, uint8_t& status, uint8_t& cc_number);
};

template <typename Transport>
template <typename Lane>
bool MIDICCLanePolicy<Transport>::next_cc(Lane const& lane, uint16_t& offset, uint8_t& status, uint8_t& cc_number) {
	if (offset + Transport::cc_span > lane.size()) {
		// Fewer than one whole message left: the scan is done.
		return false;
	}

	typename Transport::Element head[Transport::cc_span];
	for (uint16_t i = 0; i < Transport::cc_span; i++) {
		head[i] = lane.peek(static_cast<uint16_t>(offset + i));
	}
	if (!Transport::is_channel_cc(head)) {
		// Stop rather than skip, and note this is a deliberate divergence from the two scans it replaces.
		//
		// Why stop: classify_message() routes only Continuous channel CCs to this lane, so every message
		// in it is exactly cc_span wide and every offset reached here is a message boundary. Anything
		// else means the lane is not aligned to that span, and skipping on by cc_span could land the scan
		// inside a message and hand the scheduler a misaligned span to remove - corrupt MIDI on the wire.
		//
		// What stopping actually does: select_scheduled_cc() reads this as NoMore, so the scheduler still
		// picks from whatever prefix of CCs the scan already collected and pops it. It does NOT fall back
		// to FIFO order. Only an empty prefix leaves nothing to pop, and then USB skips the lane while
		// DIN aborts the pass.
		//
		// What the old scans did: USB skipped the element and carried on, so a CC behind it was still
		// scheduled. DIN skipped decodable non-CC messages by their own decoded length, and on a byte it
		// could not decode returned Invalid, which aborted the whole traversal for that slot even when
		// CCs had already been found. So old and new agree only when the offending element is first;
		// with CCs ahead of it, old DIN emitted nothing where this emits a scheduled CC.
		//
		// All of which is unreachable: classify_message() cannot put a non-CC in this lane.
		return false;
	}

	status = Transport::status(head);
	cc_number = Transport::cc_number(head);
	offset = static_cast<uint16_t>(offset + Transport::cc_span);
	return true;
}

template <typename Transport>
template <typename Lane>
bool MIDICCLanePolicy<Transport>::coalesce(Lane& lane, uint8_t status, uint8_t cc_number, uint8_t value,
                                           MIDICCQueuePolicy& debt) {
	// Find the newest queued match. Scanning unguarded is deliberate: the guard below covers only the
	// slot write, never this walk.
	int32_t latest = -1;
	uint16_t offset = 0;
	uint8_t scan_status = 0;
	uint8_t scan_cc = 0;
	while (next_cc(lane, offset, scan_status, scan_cc)) {
		if (scan_status == status && scan_cc == cc_number) {
			latest = static_cast<int32_t>(offset - Transport::cc_span);
		}
	}
	if (latest < 0) {
		return false;
	}

	{
		// A concurrent removal can advance read_pos and move the displaced head, so the offset found
		// above may now name a different message. Re-check identity under the guard rather than
		// trusting it; on a mismatch report a miss so the caller appends instead of overwriting an
		// unrelated message.
		CriticalSectionGuard guard;
		uint16_t at = static_cast<uint16_t>(latest);
		if (at + Transport::cc_span > lane.size()) {
			return false;
		}
		typename Transport::Element scratch[Transport::cc_span];
		for (uint16_t i = 0; i < Transport::cc_span; i++) {
			scratch[i] = lane.peek(static_cast<uint16_t>(at + i));
		}
		if (!Transport::is_channel_cc(scratch) || Transport::status(scratch) != status
		    || Transport::cc_number(scratch) != cc_number) {
			return false;
		}
		Transport::set_value(scratch, value);
		for (uint16_t i = 0; i < Transport::cc_span; i++) {
			lane.overwrite_at(static_cast<uint16_t>(at + i), scratch[i]);
		}
	}

	debt.bump_cc_debt(cc_number);
	return true;
}

template <typename Transport>
template <typename Lane>
bool MIDICCLanePolicy<Transport>::pop_scheduled(Lane& lane, MIDICCQueuePolicy& debt, Element* out) {
	auto begin_scan = [&lane](uint16_t& scan_position, uint16_t& limit) {
		// Offsets are elements from the lane head, so the whole lane is in range.
		scan_position = 0;
		limit = lane.size();
		return limit >= Transport::cc_span;
	};
	// next_cc bounds itself against the lane, so the cursor limit the policy carries is not needed here.
	auto next_scan = [&lane](uint16_t& scan_position, uint16_t /*limit*/, uint16_t& candidate_offset,
	                         uint8_t& cc_number) {
		uint8_t status = 0;
		if (!next_cc(lane, scan_position, status, cc_number)) {
			return MIDIQueueManager::CandidateScanResult::NoMore;
		}
		// next_cc advances past the message it reported, so back up to where that message starts.
		candidate_offset = static_cast<uint16_t>(scan_position - Transport::cc_span);
		return MIDIQueueManager::CandidateScanResult::Candidate;
	};
	auto remove_selected = [&lane](uint16_t target_offset, Element* popped_out) {
		// The producer's coalesce overwrite and this exchange are the only places both sides write the
		// same slots. Guarding just these few instructions closes that race; the scan above deliberately
		// stays outside the guard.
		CriticalSectionGuard guard;
		return lane.remove_span_via_head_swap(target_offset, Transport::cc_span, popped_out);
	};

	// The shared policy owns selection order; this supplies the transport's scan and removal shapes.
	return debt.pop_next_scheduled_cc_candidate(begin_scan, next_scan, remove_selected, out);
}

template <typename Transport>
template <typename Lane, typename AppendFn>
bool MIDICCLanePolicy<Transport>::enqueue_with_cc_policy(Lane& cc_lane, MIDICCQueuePolicy& debt, bool to_cc_lane,
                                                         bool is_channel_cc, uint8_t status, uint8_t cc_number,
                                                         uint8_t value, AppendFn&& append) {
	if (to_cc_lane && is_channel_cc && coalesce(cc_lane, status, cc_number, value, debt)) {
		// An already-queued message with this identity carries the new value, so nothing is appended.
		return true;
	}

	bool queued_ok = append();
	if (queued_ok && to_cc_lane && is_channel_cc) {
		// Record that this CC number has unsent work so scheduled dequeue can prefer it.
		debt.bump_cc_debt(cc_number);
	}
	return queued_ok;
}
