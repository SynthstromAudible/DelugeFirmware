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
#include <cstdint>
#include <optional>
#include <utility>

class InstrumentClipView;
class ModelStackWithAutoParam;
class NoteRow;
class Sound;
namespace deluge::note_locks {
struct LiveEdit;
}

/// What turning a knob does while note pads are held in the clip view (NoteLockEditor::getHeldValue)
enum class HeldNoteLock : uint8_t {
	NONE,    // Not a lockable param of this clip, or no pads held: edit the param as usual
	LOCK,    // Lock it on the held notes that play this param's Sound
	NO_NOTE, // A lockable param, but no held note plays its Sound (kit rows of other drums): leave it alone
};

/// Per-note parameter locks (model/note/note_locks.h), as entered from the instrument clip view, which owns one of
/// these: hold a note pad and turn a knob for a local (per-voice) patched param, or use the note editor's "Param
/// locks" menu.
///
/// While a pad is held its edits live in note_locks::liveEdits[i] (i = the edit pad press), heard immediately but not
/// interned. They are committed - one Pool::intern per note, one undoable action - when the press ends, and before
/// anything moves the held notes (nudge, vertical drag), so the locks move with them.
class NoteLockEditor {
public:
	explicit NoteLockEditor(InstrumentClipView& clipView) : clipView_(clipView) {}

	// Knob turns. While note pads are held, a turn of a lockable (local patched) param of this clip only ever sets
	// parameter locks, never step automation - from the mod knobs, MIDI follow and MIDI-learned knobs alike.

	/// For LOCK and NO_NOTE, gives the value to show: the held note's pending or committed lock, else the param's
	/// value at the note
	HeldNoteLock getHeldValue(ModelStackWithAutoParam* modelStack, int32_t* value);
	/// Sets this param's lock, uncommitted, on every held note playing its Sound, and makes it heard. False if no
	/// held note plays its Sound.
	bool setHeldValue(ModelStackWithAutoParam* modelStack, int32_t value);
	/// While note pads are held, a param of this clip that can't be locked (e.g. a patch cable's depth) is
	/// step-automated over the held notes as before, and the knob popup says so
	[[nodiscard]] bool automatesHeldNotes(ModelStackWithAutoParam const* modelStack);
	/// True once per pad press: show the "not lockable" notice before the automated value
	bool takeNotLockableNotice() { return std::exchange(notLockableNoticeDue_, false); }

	// Edit pad presses

	/// (Re)arms press i's live-edit slot for the notes it covers now. `samePress`: press i is the press the slot was
	/// armed for, now covering moved notes (note_locks::LiveEdits::arm)
	void arm(int32_t i, bool samePress = false);
	void rearmAll();
	/// Commits a held pad's pending lock edits to its notes as one undoable change (no-op if none) and disarms its
	/// slot. Must be called whenever a press ends.
	void commit(int32_t i);
	void commitAll();

	/// What the audition of a held note in this row plays with: the note's committed set, and the pad's live-edit slot
	struct HeldNote {
		uint16_t lockSet{0};
		const deluge::note_locks::LiveEdit* live{nullptr};
	};
	[[nodiscard]] HeldNote getHeldNote(int32_t yDisplay);
	/// The note held in this row locks a sustain (committed or still live), so it has a tail even on a sound without
	[[nodiscard]] bool heldNoteLocksSustain(int32_t yDisplay);

	// The note editor's "Param locks" menu. Edits apply to every note the note editor is targeting (held or remembered
	// pads), like the editor's other note parameters, each as its own undo step.

	/// The left-most targeted note's locks, with any pending edits
	[[nodiscard]] deluge::note_locks::LockSet getMenuLocks();
	bool setMenuLock(int32_t param, int32_t value);
	bool removeMenuLock(int32_t param);
	bool clearMenuLocks();

private:
	struct PressedRow {
		Sound* sound;
		NoteRow* row;
		int32_t noteRowId;
	};
	/// The row press i is in, if it plays a Sound (a synth, or a kit's sound drum)
	[[nodiscard]] std::optional<PressedRow> getRowOfPress(int32_t i);
	[[nodiscard]] uint16_t getCommittedLockSet(int32_t i);
	/// Pads are held in the clip view, and this is one of its params a held-pad knob turn edits
	[[nodiscard]] bool editsHeldNotes(ModelStackWithAutoParam const* modelStack);
	[[nodiscard]] int32_t findPressesFor(ModelStackWithAutoParam const* modelStack, bool* matches);
	template <typename Change>
	bool changeMenuLocks(Change change);

	InstrumentClipView& clipView_;
	bool notLockableNoticeDue_{false};
};
