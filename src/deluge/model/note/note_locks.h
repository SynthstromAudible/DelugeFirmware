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

#include <cstddef>
#include <cstdint>

/// Per-note parameter locks ("p-locks").
///
/// A lock overrides one of a Sound's *local* (per-voice) patched params for the voice that one note plays: the lock
/// value replaces the knob / automation ("preset") value, and patch cables still modulate on top of it, exactly as
/// they would on the knob value.
///
/// Storage: a Note holds a 16-bit index (Note::lockSet, in what used to be padding, so Notes stay 24 bytes) into its
/// Song's NoteLockPool. 0 means no locks. A pool entry is an immutable, deduplicated set of (param, value) pairs.
/// Because entries are never mutated or freed while the Song lives, every way a Note gets duplicated - NoteVector
/// clones, undo snapshots, clip cloning, note splitting, struct copies - just copies the index and stays valid.
/// Editing a lock interns a new set and repoints the note. Entries nothing points at any more are only reclaimed when
/// the Song is deleted; the song file only stores sets that notes reference, so a save/load compacts the pool.
///
/// Indices are only meaningful within the Song (or clipboard) pool they came from. Anything that carries notes
/// between pools (the note clipboard) must re-intern with NoteLockPool::importFrom().

namespace deluge::note_locks {

/// Max locks on one note. Bounds the per-voice copy (Voice::noteLocks).
constexpr int32_t kMaxLocksPerNote = 8;

struct Lock {
	uint8_t param; // A local patched param: deluge::modulation::params::Local, below FIRST_GLOBAL
	int32_t value; // In the patched param's preset-value domain (the same as its knob / automation value)
};

/// True if `param` can be locked: a local patched param, i.e. one with a per-voice final value.
bool isLockableParam(int32_t param);

/// Read-only view of an interned set. Locks are sorted by param, at most one per param. Only valid until the pool is
/// next modified.
struct LockSetView {
	int32_t count{0};
	const uint8_t* params{nullptr};
	const int32_t* values{nullptr};

	/// Returns nullptr if `param` isn't locked here.
	[[nodiscard]] const int32_t* find(int32_t param) const {
		for (int32_t i = 0; i < count; i++) {
			if (params[i] == param) {
				return &values[i];
			}
		}
		return nullptr;
	}
};

class Pool {
public:
	Pool() = default;
	~Pool();
	Pool(const Pool&) = delete;
	Pool& operator=(const Pool&) = delete;

	/// Interns `locks` (any order; a later duplicate param wins; unlockable params and anything past
	/// kMaxLocksPerNote are dropped) and returns its index, reusing an identical existing set. Returns 0 for an empty
	/// set or if out of memory / indices - the note then just has no locks.
	uint16_t intern(const Lock* locks, int32_t count);

	/// Count 0 for index 0 or an out-of-range index, so a stale index can never read outside the pool.
	[[nodiscard]] LockSetView get(uint16_t index) const;

	/// The set at `base` with `param` set to `value` (added or replaced). Copy-on-write: `base` itself is unchanged.
	uint16_t withLock(uint16_t base, int32_t param, int32_t value);
	/// The set at `base` without `param`; 0 if that empties it.
	uint16_t withoutLock(uint16_t base, int32_t param);
	/// Re-interns set `index` of `other` into this pool.
	uint16_t importFrom(const Pool& other, uint16_t index);

	void clear();
	[[nodiscard]] int32_t numSets() const { return numSets_ ? numSets_ - 1 : 0; }
	[[nodiscard]] size_t bytesAllocated() const;

private:
	static uint32_t hashOf(const uint8_t* params, const int32_t* values, int32_t count);
	bool reserveWords(uint32_t extra);
	bool reserveSets(uint32_t extra);
	bool rehash(uint32_t newCapacity);
	[[nodiscard]] bool equals(uint16_t index, const uint8_t* params, const int32_t* values, int32_t count) const;

	// Entry i lives at words_[offsets_[i]]: one byte count, then that many param bytes, padded to a word, then the
	// int32 values. Index 0 is the "no locks" sentinel and has no entry.
	uint32_t* words_{nullptr};
	uint32_t numWords_{0};
	uint32_t capWords_{0};
	uint32_t* offsets_{nullptr};
	uint32_t numSets_{0}; // Including the sentinel, once anything has been interned
	uint32_t capSets_{0};
	uint16_t* hash_{nullptr}; // Open addressing, 0 = empty slot. Capacity a power of two, at most half full.
	uint32_t hashCap_{0};
};

/// A note's locks as copied into the Voice playing it (pool entries can move while the voice sounds).
struct VoiceLocks {
	uint64_t mask{0}; // Bit p set if local param p is locked
	int32_t count{0};
	uint8_t params[kMaxLocksPerNote];
	int32_t values[kMaxLocksPerNote];

	[[nodiscard]] int32_t valueFor(int32_t param) const {
		for (int32_t i = 0; i < count; i++) {
			if (params[i] == param) {
				return values[i];
			}
		}
		return 0; // Not reached if the mask was checked first
	}
	void set(const LockSetView& view);
	void clear() {
		mask = 0;
		count = 0;
	}
};

/// The locks of the note currently being sent to an instrument. The sequencer sets this (through PendingScope) around
/// a note-on, and Voice::noteOn copies it, which saves threading a parameter through every instrument / drum /
/// arpeggiator note-on signature. Count 0 the rest of the time. Single-threaded: only touched from the audio routine.
extern LockSetView pending;
/// pending's index in its pool, for the arpeggiators: they remember it with each held note (ArpNote::lockSet) and set
/// it again around the note-ons they generate later.
extern uint16_t pendingIndex;

class PendingScope {
public:
	PendingScope(const Pool& pool, uint16_t index) : active_(index != 0 || pendingIndex != 0) {
		if (active_) [[unlikely]] {
			outer_ = pending;
			outerIndex_ = pendingIndex;
			pending = pool.get(index);
			pendingIndex = index;
		}
	}
	~PendingScope() {
		if (active_) [[unlikely]] {
			pending = outer_;
			pendingIndex = outerIndex_;
		}
	}
	PendingScope(const PendingScope&) = delete;
	PendingScope& operator=(const PendingScope&) = delete;

private:
	bool active_;
	uint16_t outerIndex_{0};
	LockSetView outer_;
};

} // namespace deluge::note_locks
