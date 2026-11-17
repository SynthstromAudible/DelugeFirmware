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

// Per-note parameter locks, as entered from the instrument clip view: hold a note pad and turn a knob for a local
// (per-voice) patched param. See note_locks.h for the data model.
//
// While the pad is held the edits live in note_locks::liveEdits[i] (i = the edit pad press), heard immediately but not
// interned. They are committed - one Pool::withLocks per note, one undoable action - when the press ends, which every
// way of ending a press goes through (endEditPadPress), and before anything moves the held notes (nudge, vertical
// drag), so the locks move with them.

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
#include "model/note/note_locks.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "modulation/automation/auto_param.h"
#include "modulation/params/param.h"
#include "modulation/params/param_collection.h"
#include "processing/sound/sound.h"

namespace params = deluge::modulation::params;
namespace note_locks = deluge::note_locks;

static_assert(note_locks::kMaxLiveEdits == kEditPadPressBufferSize, "one live-edit slot per edit pad press");

namespace {

/// The range of note positions a press (or remembered note editor target) covers in its row
uint32_t pressLength(const EditPadPress& press) {
	return press.isBlurredSquare ? press.intendedLength : 1;
}

/// Calls f(Note*) for each note the press covers
template <typename F>
void forEachNoteOfPress(NoteRow* row, const EditPadPress& press, F f) {
	if (row == nullptr) {
		return;
	}
	uint32_t length = pressLength(press);
	for (int32_t n = row->notes.search(press.intendedPos, GREATER_OR_EQUAL); n < row->notes.getNumElements(); n++) {
		Note* note = row->notes.getElement(n);
		if (note->pos - press.intendedPos >= length) {
			break;
		}
		f(note);
	}
}

/// Applies `change` (uint16_t(uint16_t oldLockSet)) to every note the target covers - and, when the clip is wrap
/// (cross-screen) editing, to all their copies on the other screens (NoteRow::forNotesAcrossAllScreens, the
/// note editor's other parameters' path). Part of
/// the open NOTE_EDIT action, with the row snapshotted once before the first change. `change` runs once per distinct
/// old set, so copies that shared a set share the result (one intern). If it returns kInternFailed (pool full), that
/// note keeps its locks and `*failed` is set. Returns whether any note changed.
template <typename F>
bool changeNoteLockSets(ModelStackWithNoteRow* rowStack, const EditPadPress& target, F change, bool* failed) {
	NoteRow* row = rowStack->getNoteRowAllowNull();
	if (row == nullptr) {
		return false;
	}
	constexpr int32_t kCacheSize = 8;
	uint16_t cacheFrom[kCacheSize];
	uint16_t cacheTo[kCacheSize];
	int32_t cached = 0;
	bool snapshotted = false;
	bool changed = false;

	auto apply = [&](Note* note) {
		uint16_t base = note->lockSet;
		uint16_t newLockSet;
		int32_t c = 0;
		while (c < cached && cacheFrom[c] != base) {
			c++;
		}
		if (c < cached) {
			newLockSet = cacheTo[c];
		}
		else {
			newLockSet = change(base);
			if (cached < kCacheSize) {
				cacheFrom[cached] = base;
				cacheTo[cached] = newLockSet;
				cached++;
			}
		}
		if (newLockSet == note_locks::kInternFailed) {
			*failed = true;
			return;
		}
		if (newLockSet == base) {
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
		note->lockSet = newLockSet;
		changed = true;
	};
	auto trampoline = [](Note* note, void* context) { (*static_cast<decltype(apply)*>(context))(note); };

	// Visiting the copies only changes lockSet, never moves notes, so the walk over the square stays valid
	forEachNoteOfPress(row, target,
	                   [&](Note* note) { row->forNotesAcrossAllScreens(note->pos, rowStack, trampoline, &apply); });
	return changed;
}

} // namespace

Sound* InstrumentClipView::getSoundForEditPadPress(int32_t i, NoteRow** getNoteRow, int32_t* getNoteRowId,
                                                   ParamManagerForTimeline** getParamManager) {
	InstrumentClip* clip = getCurrentInstrumentClip();
	int32_t noteRowIndex;
	NoteRow* row = clip->getNoteRowOnScreen(editPadPresses[i].yDisplay, currentSong, &noteRowIndex);
	if (getNoteRow) {
		*getNoteRow = row;
	}
	if (row == nullptr) {
		return nullptr;
	}
	if (getNoteRowId) {
		*getNoteRowId = clip->getNoteRowId(row, noteRowIndex);
	}
	ParamManagerForTimeline* paramManager;
	Sound* sound = getSoundForNoteRow(row, &paramManager);
	if (getParamManager) {
		*getParamManager = paramManager;
	}
	return sound;
}

void InstrumentClipView::armLiveNoteLocks(int32_t i, bool samePress) {
	NoteRow* row;
	int32_t noteRowId;
	if (!editPadPresses[i].isActive || getSoundForEditPadPress(i, &row, &noteRowId, nullptr) == nullptr) {
		note_locks::disarmLiveEdit(i);
		return;
	}
	InstrumentClip* clip = getCurrentInstrumentClip();
	note_locks::armLiveEdit(i, currentSong, static_cast<TimelineCounter*>(clip), noteRowId,
	                        editPadPresses[i].intendedPos, pressLength(editPadPresses[i]), clip->getWrapEditLevel(),
	                        samePress);
}

void InstrumentClipView::commitLiveNoteLocks(int32_t i) {
	note_locks::LiveEdit& live = note_locks::liveEdits[i];
	if (!live.armed()) {
		return;
	}
	InstrumentClip* clip = getCurrentInstrumentClip();
	if (live.count != 0 && live.song == currentSong && clip != nullptr
	    && live.clip == static_cast<TimelineCounter*>(clip) && editPadPresses[i].isActive) {
		// Find the notes from the press as it is now (it may have been nudged since the slot was armed)
		NoteRow* row;
		int32_t noteRowId;
		if (getSoundForEditPadPress(i, &row, &noteRowId, nullptr) != nullptr) {
			note_locks::Lock edits[note_locks::kMaxLocksPerNote];
			for (int32_t l = 0; l < live.count; l++) {
				edits[l] = {live.params[l], live.values[l]};
			}
			int32_t count = live.count;
			char modelStackMemory[MODEL_STACK_MAX_SIZE];
			ModelStackWithNoteRow* rowStack =
			    currentSong->setupModelStackWithCurrentClip(modelStackMemory)->addNoteRow(noteRowId, row);
			bool failed = false;
			bool changed = changeNoteLockSets(
			    rowStack, editPadPresses[i],
			    [&](uint16_t base) { return currentSong->noteLocks.withLocks(base, edits, count); }, &failed);
			if (changed) {
				uiNeedsRendering(this, 1 << editPadPresses[i].yDisplay, 0);
			}
			if (failed) {
				display->displayError(Error::INSUFFICIENT_RAM);
			}
		}
	}
	note_locks::disarmLiveEdit(i);
}

void InstrumentClipView::commitAllLiveNoteLocks() {
	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		commitLiveNoteLocks(i);
	}
}

void InstrumentClipView::rearmAllLiveNoteLocks() {
	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		if (editPadPresses[i].isActive) {
			armLiveNoteLocks(i, true);
		}
	}
}

void InstrumentClipView::forEachHeldLockSet(void (*fn)(uint16_t& lockSet, void* context), void* context) {
	for (EditPadPress& press : editPadPresses) {
		if (press.isActive) {
			fn(press.intendedLockSet, context);
		}
	}
}

uint16_t InstrumentClipView::getCommittedLockSetForPress(int32_t i) {
	NoteRow* row;
	if (getSoundForEditPadPress(i, &row, nullptr, nullptr) == nullptr) {
		return 0;
	}
	uint16_t lockSet = 0;
	bool found = false;
	forEachNoteOfPress(row, editPadPresses[i], [&](Note* note) {
		if (!found) {
			lockSet = note->lockSet;
			found = true;
		}
	});
	return lockSet;
}

bool InstrumentClipView::heldNoteInRowLocksSustain(int32_t yDisplay) {
	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		if (!editPadPresses[i].isActive || editPadPresses[i].yDisplay != yDisplay) {
			continue;
		}
		note_locks::LockSetView locks = currentSong->noteLocks.get(getCommittedLockSetForPress(i));
		note_locks::VoiceLocks merged;
		const note_locks::LiveEdit& live = note_locks::liveEdits[i];
		if (live.armed()) {
			locks = live.mergedWith(locks, merged);
		}
		return note_locks::locksSustain(locks.params, locks.values, locks.count);
	}
	return false;
}

const note_locks::LiveEdit* InstrumentClipView::getAuditionLiveEdit(int32_t yDisplay, uint16_t* committedLockSet) {
	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		if (editPadPresses[i].isActive && editPadPresses[i].yDisplay == yDisplay && note_locks::liveEdits[i].armed()) {
			*committedLockSet = getCommittedLockSetForPress(i);
			return &note_locks::liveEdits[i];
		}
	}
	*committedLockSet = 0;
	return nullptr;
}

/// The press whose note's value the knob shows (and matches[i] for every press playing the param's Sound), -1 if this
/// isn't a lock edit (HeldNoteLock::NONE), or -2 if it is but no held note plays the Sound (HeldNoteLock::NO_NOTE)
int32_t InstrumentClipView::findHeldNoteLockPresses(ModelStackWithAutoParam const* modelStack, bool* matches) {
	if (numEditPadPresses == 0 || !view.modLength || getRootUI() != this || modelStack == nullptr
	    || modelStack->autoParam == nullptr || modelStack->paramCollection == nullptr
	    || modelStack->paramCollection->getParamKind() != params::Kind::PATCHED
	    || !note_locks::isLockableParam(modelStack->paramId)) {
		return -1;
	}
	InstrumentClip* clip = getCurrentInstrumentClip();
	if (clip == nullptr || modelStack->getTimelineCounterAllowNull() != static_cast<TimelineCounter*>(clip)) {
		return -1;
	}
	int32_t primary = -2;
	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		matches[i] = false;
		if (!editPadPresses[i].isActive) {
			continue;
		}
		int32_t noteRowId;
		Sound* sound = getSoundForEditPadPress(i, nullptr, &noteRowId, nullptr);
		if (sound == nullptr || static_cast<ModControllable*>(sound) != modelStack->modControllable) {
			continue;
		}
		matches[i] = true;
		// The note the knob indicators were set up from (the last one pressed) is the one whose value we show
		if (primary < 0 || (noteRowId == view.modNoteRowId && editPadPresses[i].intendedPos == view.modPos)) {
			primary = i;
		}
	}
	return primary;
}

HeldNoteLock InstrumentClipView::getHeldNoteLockValue(ModelStackWithAutoParam* modelStack, int32_t* value) {
	bool matches[kEditPadPressBufferSize];
	int32_t primary = findHeldNoteLockPresses(modelStack, matches);
	if (primary == -1) {
		return HeldNoteLock::NONE;
	}
	if (primary >= 0) {
		const note_locks::LiveEdit& live = note_locks::liveEdits[primary];
		if (const int32_t* v = live.armed() ? live.find(modelStack->paramId) : nullptr) {
			*value = *v;
			return HeldNoteLock::LOCK;
		}
		if (const int32_t* v =
		        currentSong->noteLocks.get(getCommittedLockSetForPress(primary)).find(modelStack->paramId)) {
			*value = *v;
			return HeldNoteLock::LOCK;
		}
	}
	*value = modelStack->autoParam->getValuePossiblyAtPos(view.modPos, modelStack);
	return primary >= 0 ? HeldNoteLock::LOCK : HeldNoteLock::NO_NOTE;
}

bool InstrumentClipView::setHeldNoteLock(ModelStackWithAutoParam* modelStack, int32_t value) {
	bool matches[kEditPadPressBufferSize];
	if (findHeldNoteLockPresses(modelStack, matches) < 0) {
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
			armLiveNoteLocks(i);
			if (!live.armed()) {
				continue;
			}
		}
		// A note holds at most kMaxLocksPerNote locks
		note_locks::VoiceLocks merged;
		note_locks::LockSetView view =
		    live.mergedWith(currentSong->noteLocks.get(getCommittedLockSetForPress(i)), merged);
		if (view.find(param) == nullptr && view.count >= note_locks::kMaxLocksPerNote) {
			anyFull = true;
			continue;
		}
		live.set(param, value);
		// Rewrite the voices this pad is already sounding (the audition, or the sequenced note during playback)
		Sound* sound = getSoundForEditPadPress(i, nullptr, nullptr, nullptr);
		sound->updateLiveNoteLocks(live.tag, param, value, soundStack);
		editPadPresses[i].deleteOnDepress = false;
	}
	if (anyFull) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NOTE_LOCKS_FULL));
	}
	return true;
}

// Note editor ("Param locks" menu). These act on every note the note editor is targeting (held or remembered pads),
// like the editor's other note parameters, and show the left-most note's locks.

void InstrumentClipView::getNoteEditorLocks(note_locks::VoiceLocks& locks) {
	locks.clear();
	Note* note = getLeftMostNotePressed();
	if (note == nullptr) {
		return;
	}
	locks.set(currentSong->noteLocks.get(note->lockSet));
	// Plus anything pending on a held pad covering that note
	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		const note_locks::LiveEdit& live = note_locks::liveEdits[i];
		if (!editPadPresses[i].isActive || !live.armed() || live.count == 0) {
			continue;
		}
		NoteRow* row;
		if (getSoundForEditPadPress(i, &row, nullptr, nullptr) == nullptr) {
			continue;
		}
		bool covers = false;
		forEachNoteOfPress(row, editPadPresses[i], [&](Note* n) { covers = covers || n == note; });
		if (covers) {
			for (int32_t l = 0; l < live.count; l++) {
				locks.setValue(live.params[l], live.values[l]);
			}
		}
	}
}

template <typename F>
bool InstrumentClipView::changeNoteEditorLockSets(F change) {
	// Anything still pending on a held pad goes in first, so it isn't lost or applied on top of this later
	commitAllLiveNoteLocks();
	rearmAllLiveNoteLocks();
	// Each change made from the menu is its own undo step (the note editor otherwise keeps one NOTE_EDIT action
	// open for as long as notes are selected)
	actionLogger.closeAction(ActionType::NOTE_EDIT);

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	bool changed = false;
	bool failed = false;
	for (const EditPadPress& target : get_note_edit_targets()) {
		if (!target.isActive) {
			continue;
		}
		changed |= changeNoteLockSets(get_note_edit_row(modelStack, target), target, change, &failed);
	}
	actionLogger.closeAction(ActionType::NOTE_EDIT);
	if (changed) {
		uiNeedsRendering(this, 0xFFFFFFFF, 0);
	}
	if (failed) {
		display->displayError(Error::INSUFFICIENT_RAM);
	}
	return changed;
}

bool InstrumentClipView::setNoteEditorLock(int32_t param, int32_t value) {
	return changeNoteEditorLockSets([&](uint16_t base) { return currentSong->noteLocks.withLock(base, param, value); });
}

bool InstrumentClipView::removeNoteEditorLock(int32_t param) {
	return changeNoteEditorLockSets([&](uint16_t base) { return currentSong->noteLocks.withoutLock(base, param); });
}

bool InstrumentClipView::clearNoteEditorLocks() {
	return changeNoteEditorLockSets([](uint16_t) -> uint16_t { return 0; });
}
