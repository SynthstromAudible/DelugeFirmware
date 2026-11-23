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

#include "gui/views/note_lock_editor.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/ui.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note.h"
#include "model/note/note_lock_playback.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_collection.h"
#include "processing/sound/sound.h"
#include <algorithm>
#include <array>

namespace params = deluge::modulation::params;
namespace note_locks = deluge::note_locks;

static_assert(note_locks::LiveEdits::kNumSlots == kEditPadPressBufferSize, "one live-edit slot per edit pad press");

namespace {

/// The range of note positions a press (or remembered note editor target) covers in its row
uint32_t pressLength(const EditPadPress& press) {
	return press.isBlurredSquare ? press.intendedLength : 1;
}

/// Calls visit(Note&) for each note the press covers
template <typename Visit>
void forEachNoteOfPress(NoteRow& row, const EditPadPress& press, Visit visit) {
	uint32_t length = pressLength(press);
	for (int32_t n = row.notes.search(press.intendedPos, GREATER_OR_EQUAL); n < row.notes.getNumElements(); n++) {
		Note* note = row.notes.getElement(n);
		if (note->pos - press.intendedPos >= length) {
			break;
		}
		visit(*note);
	}
}

struct ChangeResult {
	bool changed{false}; // Some note's locks changed
	bool failed{false};  // Some note kept its locks because the pool is full
};

/// Gives every note the target covers - and, when the clip is wrap (cross-screen) editing, all their copies on the
/// other screens, like the note editor's other parameters - the locks `change` makes of its own: change(LockSet&)
/// edits a copy of the note's set, which is then interned. Part of the open NOTE_EDIT action, with the row snapshotted
/// once before the first change. Copies that shared a set share the result (one intern).
template <typename Change>
ChangeResult changeLocks(ModelStackWithNoteRow* rowStack, const EditPadPress& target, Change change) {
	ChangeResult result;
	NoteRow* row = rowStack->getNoteRowAllowNull();
	if (row == nullptr) {
		return result;
	}
	note_locks::Pool& pool = rowStack->song->noteLocks;

	struct Interned {
		uint16_t from;
		std::optional<uint16_t> to;
	};
	std::array<Interned, 8> cache;
	size_t numCached = 0;
	bool snapshotted = false;

	auto apply = [&](Note& note) {
		uint16_t base = note.lockSet;
		auto cached = std::find_if(cache.begin(), cache.begin() + numCached,
		                           [&](const Interned& entry) { return entry.from == base; });
		std::optional<uint16_t> newLockSet;
		if (cached != cache.begin() + numCached) {
			newLockSet = cached->to;
		}
		else {
			note_locks::LockSet locks = pool.get(base);
			change(locks);
			newLockSet = pool.intern(locks);
			if (numCached < cache.size()) {
				cache[numCached++] = {base, newLockSet};
			}
		}
		if (!newLockSet) {
			result.failed = true;
			return;
		}
		if (*newLockSet == base) {
			return;
		}
		if (!snapshotted) {
			// Undo / redo swap the snapshot back; old pool entries stay valid while the undo log can refer to them
			Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
			if (action != nullptr) {
				action->recordNoteArrayChangeIfNotAlreadySnapshotted(
				    static_cast<InstrumentClip*>(rowStack->getTimelineCounter()), rowStack->noteRowId, &row->notes,
				    false);
			}
			snapshotted = true;
		}
		note.lockSet = *newLockSet;
		result.changed = true;
	};

	// Visiting the copies only changes lockSet, never moves notes, so the walk over the square stays valid
	forEachNoteOfPress(*row, target, [&](Note& note) {
		row->forNotesAcrossAllScreens(note.pos, rowStack, [&apply](Note& copy) { apply(copy); });
	});
	return result;
}

} // namespace

std::optional<NoteLockEditor::PressedRow> NoteLockEditor::getRowOfPress(int32_t i) {
	InstrumentClip* clip = getCurrentInstrumentClip();
	int32_t noteRowIndex;
	// Accidental-aware: a press on a greyed pad targets the hidden out-of-scale row, not the in-scale one it sits on
	NoteRow* row = AccidentalRows::noteRowForPress(clipView_.editPadPresses[i], &noteRowIndex);
	if (row == nullptr) {
		return std::nullopt;
	}
	ParamManagerForTimeline* paramManager;
	Sound* sound = clipView_.getSoundForNoteRow(row, &paramManager);
	if (sound == nullptr) {
		return std::nullopt;
	}
	return PressedRow{sound, row, clip->getNoteRowId(row, noteRowIndex)};
}

void NoteLockEditor::arm(int32_t i, bool samePress) {
	const EditPadPress& press = clipView_.editPadPresses[i];
	if (!samePress) {
		notLockableNoticeDue_ = true;
	}
	std::optional<PressedRow> pressed = press.isActive ? getRowOfPress(i) : std::nullopt;
	if (!pressed) {
		note_locks::liveEdits.disarm(i);
		return;
	}
	InstrumentClip* clip = getCurrentInstrumentClip();
	note_locks::liveEdits.arm(i, currentSong, static_cast<TimelineCounter*>(clip), pressed->noteRowId,
	                          press.intendedPos, pressLength(press), clip->getWrapEditLevel(), samePress);
}

void NoteLockEditor::commit(int32_t i) {
	note_locks::LiveEdit& live = note_locks::liveEdits[i];
	if (!live.armed()) {
		return;
	}
	const EditPadPress& press = clipView_.editPadPresses[i];
	InstrumentClip* clip = getCurrentInstrumentClip();
	if (!live.edits.empty() && clip != nullptr && live.isFor(currentSong, static_cast<TimelineCounter*>(clip))
	    && press.isActive) {
		// Find the notes from the press as it is now (it may have been nudged since the slot was armed)
		if (std::optional<PressedRow> pressed = getRowOfPress(i)) {
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithNoteRow* rowStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory)
			                                      ->addNoteRow(pressed->noteRowId, pressed->row);
			ChangeResult result =
			    changeLocks(rowStack, press, [&](note_locks::LockSet& locks) { locks.merge(live.edits); });
			if (result.changed) {
				uiNeedsRendering(&clipView_, 1 << press.yDisplay, 0);
			}
			if (result.failed) {
				display->displayError(Error::INSUFFICIENT_RAM);
			}
		}
	}
	note_locks::liveEdits.disarm(i);
}

void NoteLockEditor::commitAll() {
	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		commit(i);
	}
}

void NoteLockEditor::rearmAll() {
	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		if (clipView_.editPadPresses[i].isActive) {
			arm(i, true);
		}
	}
}

uint16_t NoteLockEditor::getCommittedLockSet(int32_t i) {
	std::optional<PressedRow> pressed = getRowOfPress(i);
	if (!pressed) {
		return 0;
	}
	// The first note the press covers
	const EditPadPress& press = clipView_.editPadPresses[i];
	Note* note = pressed->row->notes.getElement(pressed->row->notes.search(press.intendedPos, GREATER_OR_EQUAL));
	return (note != nullptr && note->pos - press.intendedPos < pressLength(press)) ? note->lockSet : 0;
}

bool NoteLockEditor::heldNoteLocksSustain(int32_t yDisplay) {
	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		if (!clipView_.editPadPresses[i].isActive || clipView_.editPadPresses[i].yDisplay != yDisplay) {
			continue;
		}
		note_locks::LockSet locks = currentSong->noteLocks.get(getCommittedLockSet(i));
		if (note_locks::liveEdits[i].armed()) {
			locks.merge(note_locks::liveEdits[i].edits);
		}
		return note_locks::locksSustain(locks);
	}
	return false;
}

NoteLockEditor::HeldNote NoteLockEditor::getHeldNote(int32_t yDisplay) {
	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		if (clipView_.editPadPresses[i].isActive && clipView_.editPadPresses[i].yDisplay == yDisplay
		    && note_locks::liveEdits[i].armed()) {
			return {getCommittedLockSet(i), &note_locks::liveEdits[i]};
		}
	}
	return {};
}

/// The press whose note's value the knob shows (and matches[i] for every press playing the param's Sound), -1 if this
/// isn't a lock edit (HeldNoteLock::NONE), or -2 if it is but no held note plays the Sound (HeldNoteLock::NO_NOTE)
bool NoteLockEditor::editsHeldNotes(ModelStackWithAutoParam const* modelStack) {
	if (clipView_.numEditPadPresses == 0 || !view.modLength || getRootUI() != &clipView_ || modelStack == nullptr
	    || modelStack->autoParam == nullptr || modelStack->paramCollection == nullptr) {
		return false;
	}
	InstrumentClip* clip = getCurrentInstrumentClip();
	return clip != nullptr && modelStack->getTimelineCounterAllowNull() == static_cast<TimelineCounter*>(clip);
}

bool NoteLockEditor::automatesHeldNotes(ModelStackWithAutoParam const* modelStack) {
	return editsHeldNotes(modelStack)
	       && (modelStack->paramCollection->getParamKind() != params::Kind::PATCHED
	           || !note_locks::isLockableParam(modelStack->paramId));
}

int32_t NoteLockEditor::findPressesFor(ModelStackWithAutoParam const* modelStack, bool* matches) {
	if (!editsHeldNotes(modelStack) || automatesHeldNotes(modelStack)) {
		return -1;
	}
	int32_t primary = -2;
	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		matches[i] = false;
		if (!clipView_.editPadPresses[i].isActive) {
			continue;
		}
		std::optional<PressedRow> pressed = getRowOfPress(i);
		if (!pressed || static_cast<ModControllable*>(pressed->sound) != modelStack->modControllable) {
			continue;
		}
		matches[i] = true;
		// The note the knob indicators were set up from (the last one pressed) is the one whose value we show
		if (primary < 0
		    || (pressed->noteRowId == view.modNoteRowId && clipView_.editPadPresses[i].intendedPos == view.modPos)) {
			primary = i;
		}
	}
	return primary;
}

HeldNoteLock NoteLockEditor::getHeldValue(ModelStackWithAutoParam* modelStack, int32_t* value) {
	bool matches[kEditPadPressBufferSize];
	int32_t primary = findPressesFor(modelStack, matches);
	if (primary == -1) {
		return HeldNoteLock::NONE;
	}
	if (primary >= 0) {
		const note_locks::LiveEdit& live = note_locks::liveEdits[primary];
		std::optional<int32_t> locked = live.armed() ? live.edits.get(modelStack->paramId) : std::nullopt;
		if (!locked) {
			locked = currentSong->noteLocks.get(getCommittedLockSet(primary)).get(modelStack->paramId);
		}
		if (locked) {
			*value = *locked;
			return HeldNoteLock::LOCK;
		}
	}
	*value = modelStack->autoParam->getValuePossiblyAtPos(view.modPos, modelStack);
	return primary >= 0 ? HeldNoteLock::LOCK : HeldNoteLock::NO_NOTE;
}

bool NoteLockEditor::setHeldValue(ModelStackWithAutoParam* modelStack, int32_t value) {
	bool matches[kEditPadPressBufferSize];
	if (findPressesFor(modelStack, matches) < 0) {
		return false;
	}
	int32_t param = modelStack->paramId;
	bool anyFull = false;
	// Sound::updateLiveNoteLocks wants sound flags, which would overwrite the param part of modelStack
	char soundStackMemory[MODEL_STACK_MAX_SIZE];
	copyModelStack(soundStackMemory, modelStack, sizeof(ModelStackWithThreeMainThings));
	ModelStackWithSoundFlags* soundStack = ((ModelStackWithThreeMainThings*)soundStackMemory)->addSoundFlags();
	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		if (!matches[i]) {
			continue;
		}
		note_locks::LiveEdit& live = note_locks::liveEdits[i];
		if (!live.armed()) {
			arm(i);
			if (!live.armed()) {
				continue;
			}
		}
		// A note holds at most kMaxLocksPerNote locks
		note_locks::LockSet merged = currentSong->noteLocks.get(getCommittedLockSet(i));
		merged.merge(live.edits);
		if (!merged.has(param) && merged.full()) {
			anyFull = true;
			continue;
		}
		live.edits.set(param, value);
		// Rewrite the voices this pad is already sounding (the audition, or the sequenced note during playback)
		getRowOfPress(i)->sound->updateLiveNoteLocks(live.tag, param, value, soundStack);
		clipView_.editPadPresses[i].deleteOnDepress = false;
	}
	if (anyFull) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NOTE_LOCKS_FULL));
	}
	return true;
}

deluge::note_locks::LockSet NoteLockEditor::getMenuLocks() {
	Note* note = clipView_.getLeftMostNotePressed();
	if (note == nullptr) {
		return {};
	}
	note_locks::LockSet locks = currentSong->noteLocks.get(note->lockSet);
	// Plus anything pending on a held pad covering that note
	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		const note_locks::LiveEdit& live = note_locks::liveEdits[i];
		if (!clipView_.editPadPresses[i].isActive || !live.armed() || live.edits.empty()) {
			continue;
		}
		if (std::optional<PressedRow> pressed = getRowOfPress(i)) {
			forEachNoteOfPress(*pressed->row, clipView_.editPadPresses[i], [&](Note& covered) {
				if (&covered == note) {
					locks.merge(live.edits);
				}
			});
		}
	}
	return locks;
}

template <typename Change>
bool NoteLockEditor::changeMenuLocks(Change change) {
	// Anything still pending on a held pad goes in first, so it isn't lost or applied on top of this later
	commitAll();
	rearmAll();
	// Each change made from the menu is its own undo step (the note editor otherwise keeps one NOTE_EDIT action
	// open for as long as notes are selected)
	actionLogger.closeAction(ActionType::NOTE_EDIT);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	ChangeResult total;
	for (const EditPadPress& target : clipView_.get_note_edit_targets()) {
		if (!target.isActive) {
			continue;
		}
		ChangeResult result = changeLocks(clipView_.get_note_edit_row(modelStack, target), target, change);
		total.changed |= result.changed;
		total.failed |= result.failed;
	}
	actionLogger.closeAction(ActionType::NOTE_EDIT);
	if (total.changed) {
		uiNeedsRendering(&clipView_, 0xFFFFFFFF, 0);
	}
	if (total.failed) {
		display->displayError(Error::INSUFFICIENT_RAM);
	}
	return total.changed;
}

bool NoteLockEditor::setMenuLock(int32_t param, int32_t value) {
	return changeMenuLocks([&](note_locks::LockSet& locks) { locks.set(param, value); });
}

bool NoteLockEditor::removeMenuLock(int32_t param) {
	return changeMenuLocks([&](note_locks::LockSet& locks) { locks.remove(param); });
}

bool NoteLockEditor::clearMenuLocks() {
	return changeMenuLocks([](note_locks::LockSet& locks) { locks.clear(); });
}
