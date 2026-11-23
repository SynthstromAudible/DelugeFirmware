// Per-note parameter locks: the lock set value type, the Song's pool of them, and the path they take to a Voice.

#include "CppUTest/TestHarness.h"
#include "memory/sdram_allocator.h"
#include "model/note/note_lock_playback.h"
#include "model/note/note_locks.h"
#include "modulation/params/param.h"
#include <vector>

namespace params = deluge::modulation::params;
using namespace deluge::note_locks;

namespace {
LockSet makeSet(std::initializer_list<std::pair<int32_t, int32_t>> locks) {
	LockSet set;
	for (auto [param, value] : locks) {
		set.set(param, value);
	}
	return set;
}
} // namespace

TEST_GROUP(NoteLockSet){};

TEST(NoteLockSet, startsEmpty) {
	LockSet set;
	CHECK(set.empty());
	CHECK_EQUAL(0, set.size());
	CHECK(!set.has(params::LOCAL_PAN));
	CHECK(!set.get(params::LOCAL_PAN).has_value());
}

TEST(NoteLockSet, setKeepsParamsSortedWhateverTheOrder) {
	LockSet set = makeSet({{params::LOCAL_PAN, 3}, {params::LOCAL_OSC_A_VOLUME, 1}, {params::LOCAL_LPF_FREQ, 2}});
	CHECK_EQUAL(3, set.size());
	for (int32_t i = 1; i < set.size(); i++) {
		CHECK(set.params()[i - 1] < set.params()[i]);
	}
	CHECK_EQUAL(1, set.get(params::LOCAL_OSC_A_VOLUME).value());
	CHECK_EQUAL(2, set.get(params::LOCAL_LPF_FREQ).value());
	CHECK_EQUAL(3, set.get(params::LOCAL_PAN).value());
	CHECK(set == makeSet({{params::LOCAL_LPF_FREQ, 2}, {params::LOCAL_PAN, 3}, {params::LOCAL_OSC_A_VOLUME, 1}}));
}

TEST(NoteLockSet, setReplacesAnExistingLock) {
	LockSet set = makeSet({{params::LOCAL_PAN, 3}, {params::LOCAL_PAN, -7}});
	CHECK_EQUAL(1, set.size());
	CHECK_EQUAL(-7, set.get(params::LOCAL_PAN).value());
}

TEST(NoteLockSet, onlyLocalParamsCanBeLocked) {
	LockSet set;
	CHECK(!set.set(params::FIRST_GLOBAL, 1));
	CHECK(!set.set(-1, 1));
	CHECK(set.empty());
	CHECK(set.set(params::FIRST_GLOBAL - 1, 1));
}

TEST(NoteLockSet, aFullSetOnlyTakesReplacements) {
	LockSet set;
	for (int32_t p = 0; p < kMaxLocksPerNote; p++) {
		CHECK(set.set(p, p));
	}
	CHECK(set.full());
	CHECK(!set.set(kMaxLocksPerNote, 99));
	CHECK(!set.has(kMaxLocksPerNote));
	CHECK(set.set(0, 99));
	CHECK_EQUAL(99, set.get(0).value());
	CHECK_EQUAL(kMaxLocksPerNote, set.size());
}

TEST(NoteLockSet, removeClosesTheGap) {
	LockSet set = makeSet({{1, 10}, {5, 50}, {9, 90}});
	CHECK(set.remove(5));
	CHECK(!set.remove(5));
	CHECK(set == makeSet({{1, 10}, {9, 90}}));
	CHECK_EQUAL(paramMask(1, 9), set.mask());
}

TEST(NoteLockSet, mergeLaysTheOtherSetOnTop) {
	LockSet set = makeSet({{1, 10}, {5, 50}});
	CHECK(set.merge(makeSet({{5, 55}, {7, 70}})));
	CHECK(set == makeSet({{1, 10}, {5, 55}, {7, 70}}));
}

TEST(NoteLockSet, sustainLockAboveMinimumGivesATail) {
	CHECK(!locksSustain(LockSet{}));
	CHECK(!locksSustain(makeSet({{params::LOCAL_ENV_0_SUSTAIN, INT32_MIN}})));
	CHECK(locksSustain(makeSet({{params::LOCAL_ENV_0_SUSTAIN, 0}})));
	CHECK(!locksSustain(makeSet({{params::LOCAL_ENV_1_SUSTAIN, 0}})));
}

TEST_GROUP(NoteLockPool){void teardown() override{deluge::memory::sdramAllocationsBeforeFailure = -1;
}
}
;

TEST(NoteLockPool, emptySetIsIndexZero) {
	Pool pool;
	CHECK_EQUAL(0, pool.intern(LockSet{}).value());
	CHECK_EQUAL(0, pool.numSets());
	CHECK(pool.get(0).empty());
}

TEST(NoteLockPool, setsOfEverySizeRoundTrip) {
	Pool pool;
	std::vector<uint16_t> indices;
	for (int32_t size = 1; size <= kMaxLocksPerNote; size++) {
		LockSet set;
		for (int32_t p = 0; p < size; p++) {
			set.set(p * 3, size * 1000 - p);
		}
		indices.push_back(pool.intern(set).value());
	}
	for (int32_t size = 1; size <= kMaxLocksPerNote; size++) {
		LockSet set = pool.get(indices[size - 1]);
		CHECK_EQUAL(size, set.size());
		for (int32_t p = 0; p < size; p++) {
			CHECK_EQUAL(size * 1000 - p, set.get(p * 3).value());
		}
	}
}

TEST(NoteLockPool, equalSetsShareAnEntry) {
	Pool pool;
	uint16_t a = pool.intern(makeSet({{1, 10}, {2, 20}})).value();
	uint16_t b = pool.intern(makeSet({{2, 20}, {1, 10}})).value();
	uint16_t c = pool.intern(makeSet({{1, 10}, {2, 21}})).value();
	CHECK(a != 0);
	CHECK_EQUAL(a, b);
	CHECK(c != a);
	CHECK_EQUAL(2, pool.numSets());
}

TEST(NoteLockPool, staleIndicesReadAsNoLocks) {
	Pool pool;
	uint16_t a = pool.intern(makeSet({{1, 10}})).value();
	CHECK(pool.get(a + 1).empty());
	CHECK(pool.get(0xFFFF).empty());
}

TEST(NoteLockPool, growsPastItsFirstHashTable) {
	Pool pool;
	std::vector<uint16_t> indices;
	for (int32_t i = 0; i < 1000; i++) {
		indices.push_back(pool.intern(makeSet({{i % 40, i}, {41, -i}})).value());
	}
	CHECK_EQUAL(1000, pool.numSets());
	for (int32_t i = 0; i < 1000; i++) {
		CHECK(pool.get(indices[i]) == makeSet({{i % 40, i}, {41, -i}}));
		CHECK_EQUAL(indices[i], pool.intern(makeSet({{i % 40, i}, {41, -i}})).value());
	}
}

TEST(NoteLockPool, outOfMemoryLeavesThePoolAsItWas) {
	Pool pool;
	uint16_t a = pool.intern(makeSet({{1, 10}})).value();
	// Fail at each allocation an intern makes in turn
	int32_t failurePoints = 0;
	while (true) {
		Pool fresh;
		deluge::memory::sdramAllocationsBeforeFailure = failurePoints;
		std::optional<uint16_t> index = fresh.intern(makeSet({{2, 20}}));
		deluge::memory::sdramAllocationsBeforeFailure = -1;
		if (index.has_value()) {
			break;
		}
		failurePoints++;
		CHECK_EQUAL(0, fresh.numSets());
		CHECK_EQUAL(1, fresh.intern(makeSet({{2, 20}})).value());
		CHECK(fresh.get(1) == makeSet({{2, 20}}));
	}
	CHECK(failurePoints >= 3); // The hash table, the entries and their offsets
	deluge::memory::sdramAllocationsBeforeFailure = 0;
	CHECK_EQUAL(a, pool.intern(makeSet({{1, 10}})).value()); // An existing set needs no memory
	deluge::memory::sdramAllocationsBeforeFailure = -1;
	CHECK(pool.get(a) == makeSet({{1, 10}}));
}

TEST_GROUP(NoteLockPlayback) {
	Pool pool;
	int song{};
	int clip{};
	void teardown() override {
		for (int32_t slot = 0; slot < LiveEdits::kNumSlots; slot++) {
			liveEdits.disarm(slot);
		}
	}
};

TEST(NoteLockPlayback, scopeMakesANotesLocksPendingAndRestores) {
	uint16_t a = pool.intern(makeSet({{1, 10}})).value();
	uint16_t b = pool.intern(makeSet({{2, 20}})).value();
	POINTERS_EQUAL(nullptr, pending.locks);
	{
		PendingScope outer(pool, a, nullptr);
		CHECK(*pending.locks == makeSet({{1, 10}}));
		CHECK_EQUAL(a, pending.ref.lockSet);
		{
			PendingScope inner(pool, b, nullptr);
			CHECK(*pending.locks == makeSet({{2, 20}}));
		}
		CHECK(*pending.locks == makeSet({{1, 10}}));
		{
			PendingScope unlocked(pool, 0, nullptr);
			POINTERS_EQUAL(nullptr, pending.locks);
		}
		CHECK(*pending.locks == makeSet({{1, 10}}));
	}
	POINTERS_EQUAL(nullptr, pending.locks);
	CHECK(pending.ref == NoteRef{});
}

TEST(NoteLockPlayback, liveEditsOverlayTheCommittedSet) {
	uint16_t a = pool.intern(makeSet({{1, 10}, {2, 20}})).value();
	liveEdits.arm(0, &song, &clip, 5, 96, 24, 0);
	liveEdits[0].edits.set(2, 22);
	liveEdits[0].edits.set(3, 30);
	const LiveEdit* live = liveEdits.find(&clip, 5, 100);
	POINTERS_EQUAL(&liveEdits[0], live);
	POINTERS_EQUAL(nullptr, liveEdits.find(&clip, 5, 120));
	POINTERS_EQUAL(nullptr, liveEdits.find(&clip, 6, 100));
	{
		PendingScope scope(pool, a, live);
		CHECK(*pending.locks == makeSet({{1, 10}, {2, 22}, {3, 30}}));
		CHECK_EQUAL(liveEdits[0].tag, pending.ref.liveTag);
		NoteRef remembered = pending.ref; // As an arpeggiator would
		{
			// An unlocked note sent meanwhile mustn't inherit the held pad's edits
			PendingScope other(pool, 0, nullptr);
			POINTERS_EQUAL(nullptr, pending.locks);
			// ...but the arp replaying the held note gets them again
			PendingScope replay(pool, remembered);
			CHECK(*pending.locks == makeSet({{1, 10}, {2, 22}, {3, 30}}));
		}
	}
	POINTERS_EQUAL(nullptr, pending.locks);
}

TEST(NoteLockPlayback, wrapEditingCoversEveryScreensCopy) {
	liveEdits.arm(1, &song, &clip, 5, 96 + 384, 1, 384);
	POINTERS_EQUAL(&liveEdits[1], liveEdits.find(&clip, 5, 96));
	POINTERS_EQUAL(&liveEdits[1], liveEdits.find(&clip, 5, 96 + 768));
	POINTERS_EQUAL(nullptr, liveEdits.find(&clip, 5, 97));
}

TEST(NoteLockPlayback, rearmingTheSamePressKeepsItsTagAndEdits) {
	liveEdits.arm(2, &song, &clip, 5, 0, 1, 0);
	uint16_t tag = liveEdits[2].tag;
	liveEdits[2].edits.set(1, 10);
	liveEdits.arm(2, &song, &clip, 6, 24, 1, 0, true);
	CHECK_EQUAL(tag, liveEdits[2].tag);
	CHECK(!liveEdits[2].edits.empty());
	POINTERS_EQUAL(&liveEdits[2], liveEdits.findByTag(tag));

	liveEdits.arm(2, &song, &clip, 6, 24, 1, 0);
	CHECK(liveEdits[2].tag != tag);
	CHECK(liveEdits[2].edits.empty());
	POINTERS_EQUAL(nullptr, liveEdits.findByTag(tag));

	liveEdits.disarmAllOf(&clip);
	CHECK(!liveEdits[2].armed());
	POINTERS_EQUAL(nullptr, liveEdits.find(&clip, 6, 24));
}

TEST(NoteLockPlayback, aWaitingNotePlaysTheLocksItWasDueWith) {
	uint16_t a = pool.intern(makeSet({{1, 10}})).value();
	liveEdits.arm(3, &song, &clip, 5, 0, 1, 0);
	liveEdits[3].edits.set(2, 20);
	NoteRef note{a, liveEdits[3].tag};

	// Due now, but it has to wait for a voice: keep what it would have played
	LockSet whenDue = currentLocks(pool, note);
	CHECK(whenDue == makeSet({{1, 10}, {2, 20}}));

	// Edits made while it waits...
	liveEdits[3].edits.set(2, 99);
	liveEdits[3].edits.set(3, 30);
	CHECK(currentLocks(pool, note) == makeSet({{1, 10}, {2, 99}, {3, 30}}));

	// ...don't reach it when it starts
	{
		PendingScope scope(whenDue, note);
		CHECK(*pending.locks == makeSet({{1, 10}, {2, 20}}));
		CHECK(pending.ref == note);
		// Whatever it passes the note on to (an arpeggiator, a voice start) sees it's already pending
		PendingScope inner(pool, note);
		CHECK(*pending.locks == makeSet({{1, 10}, {2, 20}}));
		CHECK(currentLocks(pool, note) == makeSet({{1, 10}, {2, 20}}));
	}
	POINTERS_EQUAL(nullptr, pending.locks);
}
