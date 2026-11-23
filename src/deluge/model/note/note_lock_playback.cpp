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

#include "model/note/note_lock_playback.h"

namespace deluge::note_locks {

LiveEdits liveEdits;
Pending pending;

void LiveEdits::arm(int32_t slot, const void* song, const void* clip, int32_t noteRowId, uint32_t pos, uint32_t length,
                    uint32_t wrapLevel, bool samePress) {
	LiveEdit& edit = slots_[slot];
	if (!edit.armed()) {
		numArmed_++;
	}
	if (!samePress || edit.tag == 0) {
		if (++nextTag_ == 0) {
			nextTag_ = 1;
		}
		edit.tag = nextTag_;
		edit.edits.clear();
	}
	edit.song = song;
	edit.clip = clip;
	edit.noteRowId = noteRowId;
	edit.pos = pos;
	edit.length = length ? length : 1;
	edit.wrapLevel = wrapLevel ? wrapLevel : 1u << 31;
}

void LiveEdits::disarm(int32_t slot) {
	LiveEdit& edit = slots_[slot];
	if (edit.armed()) {
		numArmed_--;
	}
	edit.song = nullptr;
	edit.clip = nullptr;
	edit.edits.clear();
}

void LiveEdits::disarmAllOf(const void* owner) {
	for (int32_t slot = 0; slot < kNumSlots; slot++) {
		if (slots_[slot].armed() && (slots_[slot].song == owner || slots_[slot].clip == owner)) {
			disarm(slot);
		}
	}
}

const LiveEdit* LiveEdits::findByTag(uint16_t tag) const {
	if (tag == 0 || numArmed_ == 0) [[likely]] {
		return nullptr;
	}
	for (const LiveEdit& edit : slots_) {
		if (edit.armed() && edit.tag == tag) {
			return &edit;
		}
	}
	return nullptr;
}

const LiveEdit* LiveEdits::findArmed(const void* clip, int32_t noteRowId, uint32_t pos) const {
	for (const LiveEdit& edit : slots_) {
		if (edit.armed() && edit.matches(clip, noteRowId, pos)) {
			return &edit;
		}
	}
	return nullptr;
}

namespace {
LockSet mergedLocks(const Pool& pool, uint16_t lockSet, const LiveEdit* live) {
	LockSet locks = pool.get(lockSet);
	if (live != nullptr) {
		locks.merge(live->edits);
	}
	return locks;
}
} // namespace

LockSet currentLocks(const Pool& pool, NoteRef note) {
	if (note == pending.ref) {
		return pending.locks != nullptr ? *pending.locks : LockSet{};
	}
	return mergedLocks(pool, note.lockSet, liveEdits.findByTag(note.liveTag));
}

void PendingScope::open(const Pool& pool, uint16_t lockSet, const LiveEdit* live) {
	openWith(mergedLocks(pool, lockSet, live), {lockSet, live != nullptr ? live->tag : uint16_t{0}});
}

void PendingScope::openWith(const LockSet& locks, NoteRef note) {
	Opened& opened = opened_.emplace(pending, locks);
	pending = {opened.locks.empty() ? nullptr : &opened.locks, note};
}

} // namespace deluge::note_locks
