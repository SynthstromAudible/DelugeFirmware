#include "CppUTest/TestHarness.h"
#include "io/midi/midi_queue_manager.h"
#include <vector>

// These tests cover the transport-neutral pieces of the MIDI queue manager: the ring lane and the CC
// scheduling policy. Both are header-only, so they can be driven directly without the USB/UART layers.
//
// The CC policy talks to a transport through scan/remove callbacks. FakeCCLane stands in for that
// transport with a plain vector, mirroring what MIDIQueueManagerUSB does with packed USB-MIDI events.

namespace {

constexpr uint8_t kCCStatus = 0xB0; // CC on channel 0

/// Packs the identity fields the CC policy cares about into one queue entry.
constexpr uint32_t pack(uint8_t status, uint8_t cc_number, uint8_t value) {
	return (static_cast<uint32_t>(status) << 16) | (static_cast<uint32_t>(cc_number) << 8) | value;
}
constexpr uint8_t status_of(uint32_t e) {
	return static_cast<uint8_t>(e >> 16);
}
constexpr uint8_t cc_of(uint32_t e) {
	return static_cast<uint8_t>(e >> 8);
}
constexpr uint8_t value_of(uint32_t e) {
	return static_cast<uint8_t>(e);
}

/// Minimal stand-in for a transport's CC lane, in queued order.
struct FakeCCLane {
	std::vector<uint32_t> entries;

	bool begin(uint16_t& scan_position, uint16_t& limit) const {
		scan_position = 0;
		limit = static_cast<uint16_t>(entries.size());
		return limit > 0;
	}

	MIDIQueueManager::CandidateScanResult next_candidate(uint16_t& scan_position, uint16_t limit, uint16_t& offset,
	                                                     uint8_t& cc_number) const {
		if (scan_position >= limit) {
			return MIDIQueueManager::CandidateScanResult::NoMore;
		}
		offset = scan_position;
		cc_number = cc_of(entries[scan_position]);
		scan_position++;
		return MIDIQueueManager::CandidateScanResult::Candidate;
	}

	MIDIQueueManager::CoalesceScanResult next_coalesce(uint16_t& scan_position, uint16_t limit, uint16_t& offset,
	                                                   uint8_t& status, uint8_t& cc_number) const {
		if (scan_position >= limit) {
			return MIDIQueueManager::CoalesceScanResult::NoMore;
		}
		offset = scan_position;
		status = status_of(entries[scan_position]);
		cc_number = cc_of(entries[scan_position]);
		scan_position++;
		return MIDIQueueManager::CoalesceScanResult::Matchable;
	}

	bool remove_at(uint16_t offset, uint32_t& popped_out) {
		if (offset >= entries.size()) {
			return false;
		}
		popped_out = entries[offset];
		entries.erase(entries.begin() + offset);
		return true;
	}
};

/// Runs one scheduled CC pop against the fake lane, returning the emitted entry (0 if none).
uint32_t pop_one(MIDICCQueuePolicy& policy, FakeCCLane& lane) {
	uint32_t out = 0;
	bool ok = policy.pop_next_scheduled_cc_candidate(
	    [&lane](uint16_t& p, uint16_t& l) { return lane.begin(p, l); },
	    [&lane](uint16_t& p, uint16_t l, uint16_t& off, uint8_t& cc) { return lane.next_candidate(p, l, off, cc); },
	    [&lane](uint16_t off, uint32_t& popped) { return lane.remove_at(off, popped); }, out);
	return ok ? out : 0u;
}

/// Coalesces a value into the lane the way a transport's enqueue path does.
bool coalesce(MIDICCQueuePolicy& policy, FakeCCLane& lane, uint8_t status, uint8_t cc_number, uint8_t value) {
	return policy.coalesce_latest_matching_cc(
	    status, cc_number, [&lane](uint16_t& p, uint16_t& l) { return lane.begin(p, l); },
	    [&lane](uint16_t& p, uint16_t l, uint16_t& off, uint8_t& st, uint8_t& cc) {
		    return lane.next_coalesce(p, l, off, st, cc);
	    },
	    [&lane, status, cc_number, value](uint16_t off) {
		    // Mirrors the transports: re-validate identity before overwriting, and report a miss otherwise.
		    if (off >= lane.entries.size() || status_of(lane.entries[off]) != status
		        || cc_of(lane.entries[off]) != cc_number) {
			    return false;
		    }
		    lane.entries[off] = pack(status, cc_number, value);
		    return true;
	    });
}

} // namespace

// --- Ring lane mechanics ---

TEST_GROUP(MIDIQueueLaneBasics){};

TEST(MIDIQueueLaneBasics, PushPopRoundTrip) {
	MIDIQueueLane<uint32_t, 8> lane;
	CHECK_TRUE(lane.empty());
	CHECK_TRUE(lane.push(11));
	CHECK_TRUE(lane.push(22));
	CHECK_EQUAL(2, lane.size());

	uint32_t out = 0;
	CHECK_TRUE(lane.pop(out));
	CHECK_EQUAL(11, out);
	CHECK_TRUE(lane.pop(out));
	CHECK_EQUAL(22, out);
	CHECK_TRUE(lane.empty());
}

TEST(MIDIQueueLaneBasics, KeepsOneSlotFreeSoFullIsDistinctFromEmpty) {
	MIDIQueueLane<uint32_t, 8> lane;
	for (uint32_t i = 0; i < 7; i++) {
		CHECK_TRUE(lane.push(i));
	}
	CHECK_EQUAL(7, lane.size());
	CHECK_EQUAL(0, lane.space());
	CHECK_FALSE(lane.push(99)); // capacity is Capacity-1
	CHECK_FALSE(lane.empty());
}

TEST(MIDIQueueLaneBasics, PeekIsRelativeToHeadAcrossWrap) {
	MIDIQueueLane<uint32_t, 8> lane;
	// Drive read_pos forward so the logical span wraps the physical ring.
	for (uint32_t i = 0; i < 6; i++) {
		lane.push(i);
	}
	uint32_t sink = 0;
	for (int i = 0; i < 5; i++) {
		lane.pop(sink);
	}
	lane.push(100);
	lane.push(200);
	CHECK_EQUAL(3, lane.size());
	CHECK_EQUAL(5, lane.peek(0));
	CHECK_EQUAL(100, lane.peek(1));
	CHECK_EQUAL(200, lane.peek(2));
}

TEST(MIDIQueueLaneBasics, PopManyIsAllOrNothing) {
	MIDIQueueLane<uint32_t, 8> lane;
	lane.push(1);
	lane.push(2);
	uint32_t out[3] = {0, 0, 0};
	CHECK_FALSE(lane.pop_many(out, 3)); // more than queued: must not partially consume
	CHECK_EQUAL(2, lane.size());
	CHECK_TRUE(lane.pop_many(out, 2));
	CHECK_EQUAL(1, out[0]);
	CHECK_EQUAL(2, out[1]);
	CHECK_TRUE(lane.empty());
}

// --- CC scheduling policy ---

TEST_GROUP(MIDICCScheduling){};

TEST(MIDICCScheduling, WithNoDebtSelectionIsRoundRobinByCCNumber) {
	MIDICCQueuePolicy policy;
	FakeCCLane lane;
	// Queued out of CC-number order; with no debt the policy walks CC numbers from next_cc_number.
	lane.entries = {pack(kCCStatus, 40, 1), pack(kCCStatus, 10, 2), pack(kCCStatus, 25, 3)};

	CHECK_EQUAL(10, cc_of(pop_one(policy, lane)));
	CHECK_EQUAL(25, cc_of(pop_one(policy, lane)));
	CHECK_EQUAL(40, cc_of(pop_one(policy, lane)));
}

TEST(MIDICCScheduling, DebtOverridesRoundRobinOrder) {
	MIDICCQueuePolicy policy;
	FakeCCLane lane;
	lane.entries = {pack(kCCStatus, 10, 1), pack(kCCStatus, 25, 2), pack(kCCStatus, 40, 3)};
	// CC40 changed while queued, so it should jump ahead of lower-numbered candidates.
	policy.bump_cc_debt(40);

	CHECK_EQUAL(40, cc_of(pop_one(policy, lane)));
	// Debt is cleared on send, so the remainder falls back to round-robin.
	CHECK_EQUAL(10, cc_of(pop_one(policy, lane)));
	CHECK_EQUAL(25, cc_of(pop_one(policy, lane)));
}

TEST(MIDICCScheduling, HighestDebtWins) {
	MIDICCQueuePolicy policy;
	FakeCCLane lane;
	lane.entries = {pack(kCCStatus, 10, 1), pack(kCCStatus, 25, 2), pack(kCCStatus, 40, 3)};
	policy.bump_cc_debt(10);
	policy.bump_cc_debt(25);
	policy.bump_cc_debt(25);
	policy.bump_cc_debt(40);

	CHECK_EQUAL(25, cc_of(pop_one(policy, lane)));
}

TEST(MIDICCScheduling, PopClearsDebtForTheServicedCC) {
	MIDICCQueuePolicy policy;
	FakeCCLane lane;
	lane.entries = {pack(kCCStatus, 10, 1), pack(kCCStatus, 40, 2)};
	policy.bump_cc_debt(40);
	policy.bump_cc_debt(40);
	CHECK_EQUAL(40, cc_of(pop_one(policy, lane)));

	// Re-queue CC40 with no fresh debt; CC10 should now win on round-robin.
	lane.entries.push_back(pack(kCCStatus, 40, 3));
	CHECK_EQUAL(10, cc_of(pop_one(policy, lane)));
}

TEST(MIDICCScheduling, EmptyLaneYieldsNothing) {
	MIDICCQueuePolicy policy;
	FakeCCLane lane;
	uint32_t out = 0;
	bool ok = policy.pop_next_scheduled_cc_candidate(
	    [&lane](uint16_t& p, uint16_t& l) { return lane.begin(p, l); },
	    [&lane](uint16_t& p, uint16_t l, uint16_t& off, uint8_t& cc) { return lane.next_candidate(p, l, off, cc); },
	    [&lane](uint16_t off, uint32_t& popped) { return lane.remove_at(off, popped); }, out);
	CHECK_FALSE(ok);
}

// --- Coalescing ---

TEST_GROUP(MIDICCCoalescing){};

TEST(MIDICCCoalescing, UpdatesQueuedValueInPlaceAndKeepsPosition) {
	MIDICCQueuePolicy policy;
	FakeCCLane lane;
	lane.entries = {pack(kCCStatus, 10, 1), pack(kCCStatus, 40, 2), pack(kCCStatus, 25, 3)};

	CHECK_TRUE(coalesce(policy, lane, kCCStatus, 40, 99));
	CHECK_EQUAL(3, lane.entries.size());        // no new entry appended
	CHECK_EQUAL(40, cc_of(lane.entries[1]));    // still the same entry, in the same slot
	CHECK_EQUAL(99, value_of(lane.entries[1])); // value replaced, position kept
}

TEST(MIDICCCoalescing, ReportsMissWhenNothingMatches) {
	MIDICCQueuePolicy policy;
	FakeCCLane lane;
	lane.entries = {pack(kCCStatus, 10, 1)};
	CHECK_FALSE(coalesce(policy, lane, kCCStatus, 77, 5));
	CHECK_FALSE(coalesce(policy, lane, 0xB1, 10, 5)); // different channel is a different identity
}

TEST(MIDICCCoalescing, CoalescedCCIsPreferredOnNextPop) {
	MIDICCQueuePolicy policy;
	FakeCCLane lane;
	lane.entries = {pack(kCCStatus, 10, 1), pack(kCCStatus, 25, 2), pack(kCCStatus, 40, 3)};

	// Coalescing bumps debt, which is what makes an actively-moving control jump the queue.
	CHECK_TRUE(coalesce(policy, lane, kCCStatus, 40, 77));
	uint32_t sent = pop_one(policy, lane);
	CHECK_EQUAL(40, cc_of(sent));
	CHECK_EQUAL(77, value_of(sent)); // and it carries the freshest value
}

// --- Out-of-order removal ---
//
// Scheduled CC dequeue removes entries from the middle of a lane. It must do so without touching
// write_pos, which the producer owns (consume_queued_messages() runs in an ISR and would otherwise race a
// mainline push()), and it must free the slot immediately rather than leaving it dead in place.

TEST_GROUP(MIDIQueueLaneOutOfOrderRemoval){};

TEST(MIDIQueueLaneOutOfOrderRemoval, RemovalLeavesWritePosUntouched) {
	MIDIQueueLane<uint32_t, 16> lane;
	for (uint32_t i = 1; i <= 5; i++) {
		lane.push(i);
	}
	uint16_t write_pos_before = lane.write_pos;

	uint32_t removed[1] = {0};
	CHECK_TRUE(lane.remove_span_via_head_swap(2, 1, removed));
	CHECK_EQUAL(3, removed[0]);
	CHECK_EQUAL(write_pos_before, lane.write_pos); // the producer's index must not move
}

TEST(MIDIQueueLaneOutOfOrderRemoval, RemovalFreesTheSlotImmediately) {
	MIDIQueueLane<uint32_t, 16> lane;
	for (uint32_t i = 1; i <= 5; i++) {
		lane.push(i);
	}
	uint16_t space_before = lane.space();
	uint32_t removed[1] = {0};
	CHECK_TRUE(lane.remove_span_via_head_swap(2, 1, removed));
	CHECK_EQUAL(4, lane.size());
	CHECK_EQUAL(space_before + 1, lane.space()); // capacity is reclaimed, not leaked
}

TEST(MIDIQueueLaneOutOfOrderRemoval, RepeatedRemovalDoesNotGrowTheLane) {
	// The pathological pattern: one cold entry parked at the head while a hot entry is repeatedly removed
	// and re-queued. Leaving removed slots dead in place would leak one slot per cycle and eventually
	// fill the lane, dropping MIDI while only two entries were ever live.
	MIDIQueueLane<uint32_t, 16> lane;
	lane.push(0xC01D);
	lane.push(0x0BEEF);
	for (int cycle = 0; cycle < 50; cycle++) {
		uint16_t offset = static_cast<uint16_t>(lane.size() - 1);
		uint32_t removed[1] = {0};
		CHECK_TRUE(lane.remove_span_via_head_swap(offset, 1, removed));
		CHECK_TRUE(lane.push(0x0BEEF));
		CHECK_EQUAL(2, lane.size()); // bounded, every cycle
	}
}

TEST(MIDIQueueLaneOutOfOrderRemoval, RemovingTheHeadIsAPlainPop) {
	MIDIQueueLane<uint32_t, 16> lane;
	lane.push(1);
	lane.push(2);
	lane.push(3);
	uint32_t removed[1] = {0};
	CHECK_TRUE(lane.remove_span_via_head_swap(0, 1, removed));
	CHECK_EQUAL(1, removed[0]);
	CHECK_EQUAL(2, lane.size());
	CHECK_EQUAL(2, lane.peek(0));
	CHECK_EQUAL(3, lane.peek(1));
}

TEST(MIDIQueueLaneOutOfOrderRemoval, RemovesAMultiEntrySpanAsAUnit) {
	// DIN removes a whole three-byte CC message at once.
	MIDIQueueLane<uint8_t, 16> lane;
	uint8_t bytes[] = {0xB0, 10, 1, 0xB0, 20, 2, 0xB0, 30, 3};
	for (uint8_t b : bytes) {
		lane.push(b);
	}
	uint8_t removed[3] = {0, 0, 0};
	CHECK_TRUE(lane.remove_span_via_head_swap(3, 3, removed));
	CHECK_EQUAL(0xB0, removed[0]);
	CHECK_EQUAL(20, removed[1]);
	CHECK_EQUAL(2, removed[2]);
	CHECK_EQUAL(6, lane.size()); // all three bytes freed together
}

TEST(MIDIQueueLaneOutOfOrderRemoval, SurvivingEntriesAreAllStillPresent) {
	MIDIQueueLane<uint32_t, 16> lane;
	for (uint32_t i = 1; i <= 5; i++) {
		lane.push(i);
	}
	uint32_t removed[1] = {0};
	lane.remove_span_via_head_swap(3, 1, removed); // removes "4"
	CHECK_EQUAL(4, removed[0]);

	// The displaced head moves into the vacated slot, so position shifts, but nothing is lost or
	// duplicated - which is the property that actually matters for queued MIDI.
	bool seen[6] = {false, false, false, false, false, false};
	CHECK_EQUAL(4, lane.size());
	for (uint16_t i = 0; i < lane.size(); i++) {
		uint32_t v = lane.peek(i);
		CHECK(v >= 1 && v <= 5);
		CHECK_FALSE(seen[v]);
		seen[v] = true;
	}
	CHECK_FALSE(seen[4]); // the removed entry is gone
}

TEST(MIDIQueueLaneOutOfOrderRemoval, RejectsOutOfRangeSpans) {
	MIDIQueueLane<uint32_t, 16> lane;
	lane.push(1);
	lane.push(2);
	uint32_t removed[3] = {0, 0, 0};
	CHECK_FALSE(lane.remove_span_via_head_swap(0, 3, removed)); // wider than the queue
	CHECK_FALSE(lane.remove_span_via_head_swap(2, 1, removed)); // past the end
	CHECK_EQUAL(2, lane.size());
}

// --- Coalescing races the consumer ---

TEST(MIDICCCoalescing, ReportsMissWhenTheMatchedEntryWasRemovedConcurrently) {
	MIDICCQueuePolicy policy;
	FakeCCLane lane;
	lane.entries = {pack(kCCStatus, 10, 1), pack(kCCStatus, 40, 2)};

	// The scan finds CC40, but the transport's guarded re-check sees a different identity in that slot -
	// which is what happens when the consumer tombstones the entry and drops leading tombstones between
	// the scan and the write. Coalescing must report a miss so the caller appends instead.
	bool coalesced = policy.coalesce_latest_matching_cc(
	    kCCStatus, 40, [&lane](uint16_t& p, uint16_t& l) { return lane.begin(p, l); },
	    [&lane](uint16_t& p, uint16_t l, uint16_t& off, uint8_t& st, uint8_t& cc) {
		    return lane.next_coalesce(p, l, off, st, cc);
	    },
	    [](uint16_t) { return false; });
	CHECK_FALSE(coalesced);
}

TEST(MIDICCCoalescing, MissedCoalesceDoesNotAwardDebt) {
	MIDICCQueuePolicy policy;
	FakeCCLane lane;
	lane.entries = {pack(kCCStatus, 10, 1), pack(kCCStatus, 40, 2)};

	// A failed coalesce must not leave debt behind, or CC40 would jump the queue on the strength of an
	// update that was never actually applied.
	policy.coalesce_latest_matching_cc(
	    kCCStatus, 40, [&lane](uint16_t& p, uint16_t& l) { return lane.begin(p, l); },
	    [&lane](uint16_t& p, uint16_t l, uint16_t& off, uint8_t& st, uint8_t& cc) {
		    return lane.next_coalesce(p, l, off, st, cc);
	    },
	    [](uint16_t) { return false; });

	CHECK_EQUAL(10, cc_of(pop_one(policy, lane))); // round-robin order, no debt preference
}

TEST(MIDICCScheduling, SelectionVisitsEachEntryExactlyOncePerPop) {
	// Selection used to fill a 128-entry map, scan the lane, then walk all 128 CC numbers - per pop, and up
	// to eight times per transfer, with interrupts masked. It is now a single pass, so the scan callback
	// should fire once per entry (plus the terminating NoMore).
	MIDICCQueuePolicy policy;
	FakeCCLane lane;
	for (uint8_t cc = 0; cc < 12; cc++) {
		lane.entries.push_back(pack(kCCStatus, static_cast<uint8_t>(cc * 3), 1));
	}

	int scan_calls = 0;
	uint32_t out = 0;
	bool ok = policy.pop_next_scheduled_cc_candidate(
	    [&lane](uint16_t& p, uint16_t& l) { return lane.begin(p, l); },
	    [&lane, &scan_calls](uint16_t& p, uint16_t l, uint16_t& off, uint8_t& cc) {
		    scan_calls++;
		    return lane.next_candidate(p, l, off, cc);
	    },
	    [&lane](uint16_t off, uint32_t& popped) { return lane.remove_at(off, popped); }, out);

	CHECK_TRUE(ok);
	CHECK_EQUAL(13, scan_calls); // 12 entries + one NoMore, i.e. exactly one traversal
}
