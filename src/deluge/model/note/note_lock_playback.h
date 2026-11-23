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

#include "model/note/note_locks.h"
#include <array>
#include <cstdint>
#include <optional>

/// How a note's parameter locks (note_locks.h) get from the sequencer to the Voice that plays the note.

namespace deluge::note_locks {

/// What something that plays a note later (an arpeggiator) remembers of its locks
struct NoteRef {
	uint16_t lockSet{0}; // The note's committed set: Note::lockSet, an index into the Song's Pool
	uint16_t liveTag{0}; // LiveEdit::tag of the held pad whose uncommitted edits the note hears too, else 0

	bool operator==(const NoteRef&) const = default;
};

/// Uncommitted lock edits for one held note pad in the clip view ("live" edits). While the pad is held, knob turns go
/// into one of these fixed slots - values only, no pool indices, no allocation - and are heard straight away: the
/// sequencer and the audition overlay them on the note's committed set, and Sound::updateLiveNoteLocks rewrites the
/// voices already sounding. On pad release the clip view commits a slot with one Pool::intern per note, as one
/// undoable action, and disarms it.
///
/// A slot is "armed" for as long as its pad is held, even before anything is edited, so voices started meanwhile get
/// its tag and the first knob turn can reach them. The identity fields are only ever compared, never dereferenced, so
/// a stale slot can't touch freed memory; Song and InstrumentClip destructors disarm slots pointing at them anyway.
struct LiveEdit {
	LockSet edits;
	uint16_t tag{0}; // Voices started with this slot's locks carry this tag (VoiceNoteLocks::liveEditTag)

	[[nodiscard]] bool armed() const { return song != nullptr; }
	[[nodiscard]] bool isFor(const void* song_, const void* clip_) const { return song == song_ && clip == clip_; }
	/// Whether this slot's edits apply to the note at `pos_` of that row
	[[nodiscard]] bool matches(const void* clip_, int32_t noteRowId_, uint32_t pos_) const {
		if (clip != clip_ || noteRowId != noteRowId_) {
			return false;
		}
		// With wrap (cross-screen) editing, every copy (pos +- n * wrapLevel) gets the edits too, as it will on commit
		uint32_t base = pos % wrapLevel;
		uint32_t offset = pos_ % wrapLevel;
		return (offset >= base ? offset - base : offset + wrapLevel - base) < length;
	}

private:
	friend class LiveEdits;
	const void* song{nullptr}; // nullptr = not armed
	const void* clip{nullptr}; // The InstrumentClip, as a TimelineCounter* (what the sequencer's model stack holds)
	int32_t noteRowId{0};
	uint32_t pos{0}; // Applies to the notes in [pos, pos + length) of that row
	uint32_t length{1};
	uint32_t wrapLevel{1u << 31}; // InstrumentClip::getWrapEditLevel(): huge when not wrap editing
};

/// The live-edit slots: one per clip-view edit pad press (kEditPadPressBufferSize), indexed like editPadPresses
class LiveEdits {
public:
	static constexpr int32_t kNumSlots = 8;

	[[nodiscard]] LiveEdit& operator[](int32_t slot) { return slots_[slot]; }

	/// Arms `slot` for the notes at [pos, pos + length) of that row. `samePress`: the press it was armed for is just
	/// being pointed at where its notes are now (after a nudge, a vertical drag, or a commit) - keep the slot's tag,
	/// so voices already started from it still hear its edits, and any edits not yet committed. Otherwise it's a new
	/// press: new tag, no edits.
	void arm(int32_t slot, const void* song, const void* clip, int32_t noteRowId, uint32_t pos, uint32_t length,
	         uint32_t wrapLevel, bool samePress = false);
	/// Drops the slot's edits. Its tag stays, so re-arming the same press keeps reaching the voices it started.
	void disarm(int32_t slot);
	/// Disarms every slot whose song or clip is `owner` (called as a Song / InstrumentClip is destroyed)
	void disarmAllOf(const void* owner);

	/// The armed slot covering this note, or nullptr. Cheap when no pad is held, which is how the sequencer calls it.
	[[nodiscard]] const LiveEdit* find(const void* clip, int32_t noteRowId, uint32_t pos) const {
		if (numArmed_ == 0) [[likely]] {
			return nullptr;
		}
		return findArmed(clip, noteRowId, pos);
	}
	/// The armed slot with this tag, or nullptr (also for tag 0)
	[[nodiscard]] const LiveEdit* findByTag(uint16_t tag) const;

private:
	[[nodiscard]] const LiveEdit* findArmed(const void* clip, int32_t noteRowId, uint32_t pos) const;

	std::array<LiveEdit, kNumSlots> slots_;
	int32_t numArmed_{0};
	uint16_t nextTag_{0};
};

extern LiveEdits liveEdits;

/// The locks of the note currently being sent to an instrument. The sequencer sets this (through PendingScope) around
/// a note-on, and the Voice it starts copies it (VoiceNoteLocks::takePending), which saves threading a parameter
/// through every instrument / drum / arpeggiator note-on signature. Empty the rest of the time. Single-threaded: set
/// by the sequencer in the audio routine and by the clip view's audition note-on, and every scope is closed before
/// either returns.
struct Pending {
	const LockSet* locks{nullptr}; // nullptr if the note has none
	NoteRef ref;                   // Where they came from, for the arpeggiators to remember (ArpNote::locks)
};
extern Pending pending;

/// The locks a note plays with right now: what's pending if it is the pending note, else its committed set with its
/// held pad's live edits on top. For keeping with a note that has to wait for its voice.
[[nodiscard]] LockSet currentLocks(const Pool& pool, NoteRef note);

/// Makes one note's locks `pending` for as long as it's in scope, then puts back what was pending before
class PendingScope {
public:
	/// A note that may have live (uncommitted) edits from a held pad: `live` is that pad's slot (LiveEdits::find), or
	/// nullptr. Does nothing if this is what's pending already - in particular in the usual case of no locks at all.
	PendingScope(const Pool& pool, uint16_t lockSet, const LiveEdit* live) {
		if (live != nullptr || NoteRef{lockSet, 0} != pending.ref) [[unlikely]] {
			open(pool, lockSet, live);
		}
	}
	/// A note an arpeggiator plays: the locks of the held note it came from. Does nothing if that's what's pending
	/// already - when nothing has locks, or when the arp plays the very note the sequencer is sending. Any other note,
	/// even one with the same committed set, gets its own locks, so a held pad's edits never leak onto a different
	/// note the arp picks.
	PendingScope(const Pool& pool, NoteRef note) {
		if (note != pending.ref) [[unlikely]] {
			open(pool, note.lockSet, liveEdits.findByTag(note.liveTag));
		}
	}
	/// A note that was due earlier and had to wait (DeferredNoteOns): `locks` is what its locks were when it was due,
	/// which it plays with whatever has been edited since
	PendingScope(const LockSet& locks, NoteRef note) { openWith(locks, note); }
	~PendingScope() {
		if (opened_) [[unlikely]] {
			pending = opened_->outer;
		}
	}
	PendingScope(const PendingScope&) = delete;
	PendingScope& operator=(const PendingScope&) = delete;

private:
	void open(const Pool& pool, uint16_t lockSet, const LiveEdit* live);
	void openWith(const LockSet& locks, NoteRef note);

	struct Opened {
		Pending outer;
		LockSet locks; // What `pending.locks` points at while this scope is open
	};
	std::optional<Opened> opened_;
};

} // namespace deluge::note_locks
