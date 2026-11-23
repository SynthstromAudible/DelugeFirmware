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

#include "memory/sdram_allocator.h"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

/// Per-note parameter locks ("p-locks").
///
/// A lock overrides one of a Sound's *local* (per-voice) patched params for the voice that one note plays: the lock
/// value replaces the knob / automation ("preset") value, and patch cables still modulate on top of it, exactly as
/// they would on the knob value.
///
/// Storage: a Note holds a 16-bit index (Note::lockSet, in what used to be padding, so Notes stay 24 bytes) into its
/// Song's Pool. 0 means no locks. A pool entry is an immutable, deduplicated LockSet. Because entries are never
/// mutated or freed while the Song lives, every way a Note gets duplicated - NoteVector clones, undo snapshots, clip
/// cloning, note splitting, struct copies - just copies the index and stays valid. Editing a lock interns a new set
/// and repoints the note - once per edit gesture (see note_lock_playback.h's LiveEdit), adding at most one small
/// entry. Entries are reclaimed only when the Song is deleted and by a save / load (the song file only stores sets
/// that notes reference); during a session the pool only grows, capped at 65534 sets.
///
/// Indices are only meaningful within the Song (or clipboard) pool they came from. Anything that carries notes
/// between pools (the note clipboard) re-interns: `to.intern(from.get(index))`.

namespace deluge::note_locks {

/// Max locks on one note
constexpr int32_t kMaxLocksPerNote = 8;

/// True if `param` can be locked: a local patched param (deluge::modulation::params::Local, below FIRST_GLOBAL),
/// i.e. one with a per-voice final value.
bool isLockableParam(int32_t param);

/// A bit per param, as LockSet::mask() has them
template <typename... Params>
constexpr uint64_t paramMask(Params... params) {
	return ((uint64_t{1} << static_cast<uint32_t>(params)) | ...);
}

/// A note's parameter locks: up to kMaxLocksPerNote (param, value) pairs, one per param, kept sorted by param (so
/// equal sets are identical). A value is in the patched param's preset-value domain, the same as its knob /
/// automation value. This is a plain value type: it's what a pool entry holds, what a Voice plays with, and what a
/// held pad's uncommitted edits are.
class LockSet {
public:
	[[nodiscard]] int32_t size() const { return count_; }
	[[nodiscard]] bool empty() const { return mask_ == 0; }
	[[nodiscard]] bool full() const { return count_ == kMaxLocksPerNote; }
	[[nodiscard]] std::span<const uint8_t> params() const { return {params_.data(), count_}; }
	[[nodiscard]] std::span<const int32_t> values() const { return {values_.data(), count_}; }

	/// Bit p set if param p is locked
	[[nodiscard]] uint64_t mask() const { return mask_; }
	[[nodiscard]] bool has(int32_t param) const { return ((mask_ >> param) & 1) != 0; }
	[[nodiscard]] bool hasAnyOf(uint64_t paramMask) const { return (mask_ & paramMask) != 0; }

	/// The locked value of `param`, if it's locked
	[[nodiscard]] std::optional<int32_t> get(int32_t param) const {
		if (!has(param)) [[likely]] {
			return std::nullopt;
		}
		int32_t i = 0;
		while (params_[i] != param) {
			i++;
		}
		return values_[i];
	}

	/// Adds or replaces one lock. False (and no change) if `param` can't be locked, or it's a new one and the set is
	/// full.
	bool set(int32_t param, int32_t value);
	/// False if `param` wasn't locked
	bool remove(int32_t param);
	/// Sets each of `other`'s locks, on top of these. False if any of them didn't fit.
	bool merge(const LockSet& other);
	void clear() { *this = LockSet{}; }

	[[nodiscard]] bool operator==(const LockSet& other) const;

private:
	// The mask comes first, and is all that the checks made on a voice without locks (the usual case) read
	uint64_t mask_{0};
	uint8_t count_{0};
	std::array<uint8_t, kMaxLocksPerNote> params_{};
	std::array<int32_t, kMaxLocksPerNote> values_{};
};

/// True if `locks` lock envelope 1's sustain above its minimum. Such a note sustains even on a sound whose own
/// envelope doesn't, so it gets a note tail (and a note-off) there regardless: the clip view lets you set its length
/// and draws its tail, and the sequencer ends it there.
bool locksSustain(const LockSet& locks);

/// The lock sets of every Note in one Song (or of the notes on the clipboard). Sets are only ever added.
class Pool {
public:
	Pool() = default;
	Pool(Pool&&) = default;
	Pool& operator=(Pool&&) = default;
	Pool(const Pool&) = delete;
	Pool& operator=(const Pool&) = delete;

	/// The index of the entry equal to `locks`, adding one if there isn't one yet. 0 for an empty set. nullopt if the
	/// pool is out of memory or indices: the caller should then leave its note's locks as they were.
	[[nodiscard]] std::optional<uint16_t> intern(const LockSet& locks);

	/// Empty for index 0 or an out-of-range index, so a stale index can never read outside the pool
	[[nodiscard]] LockSet get(uint16_t index) const;

	[[nodiscard]] int32_t numSets() const { return offsets_.empty() ? 0 : static_cast<int32_t>(offsets_.size()) - 1; }

private:
	template <typename T>
	using Array = std::vector<T, memory::sdram_allocator<T>>;

	[[nodiscard]] uint16_t lookup(uint32_t hash, const LockSet& locks) const;
	void addToHash(uint32_t hash, uint16_t index);
	void rehash(uint32_t newCapacity);

	// Entry i lives at words_[offsets_[i]]: one byte count, then that many param bytes, padded to a word, then the
	// int32 values. Index 0 is the "no locks" sentinel and has no entry.
	Array<uint32_t> words_;
	Array<uint32_t> offsets_; // Including the sentinel, once anything has been interned
	Array<uint16_t> hash_;    // Open addressing, 0 = empty slot. Size a power of two, at most half full.
};

} // namespace deluge::note_locks
