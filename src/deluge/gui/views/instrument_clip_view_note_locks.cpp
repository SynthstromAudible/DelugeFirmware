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

/// Applies `change` (uint16_t(uint16_t oldLockSet)) to every note the target covers, as part of the open NOTE_EDIT
/// action. Returns whether any note changed.
template <typename F>
bool changeNoteLockSets(InstrumentClip* clip, int32_t noteRowId, NoteRow* row, const EditPadPress& target, F change) {
	if (row == nullptr) {
		return false;
	}
	bool snapshotted = false;
	bool changed = false;
	forEachNoteOfPress(row, target, [&](Note* note) {
		uint16_t newLockSet = change(note->lockSet);
		if (newLockSet == note->lockSet) {
			return;
		}
		if (!snapshotted) {
			// Snapshot the row's notes (lock set indices included) once, before the first change. Undo / redo swap
			// the snapshot back, and old pool entries stay valid while anything in the undo log can refer to them.
			Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
			if (action != nullptr) {
				action->recordNoteArrayChangeIfNotAlreadySnapshotted(clip, noteRowId, &row->notes, false);
			}
			snapshotted = true;
		}
		note->lockSet = newLockSet;
		changed = true;
	});
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

void InstrumentClipView::armLiveNoteLocks(int32_t i) {
	NoteRow* row;
	int32_t noteRowId;
	if (!editPadPresses[i].isActive || getSoundForEditPadPress(i, &row, &noteRowId, nullptr) == nullptr) {
		note_locks::disarmLiveEdit(i);
		return;
	}
	InstrumentClip* clip = getCurrentInstrumentClip();
	note_locks::armLiveEdit(i, currentSong, static_cast<TimelineCounter*>(clip), noteRowId,
	                        editPadPresses[i].intendedPos, pressLength(editPadPresses[i]));
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
			bool changed = changeNoteLockSets(clip, noteRowId, row, editPadPresses[i], [&](uint16_t base) {
				return currentSong->noteLocks.withLocks(base, edits, count);
			});
			if (changed) {
				uiNeedsRendering(this, 1 << editPadPresses[i].yDisplay, 0);
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
			armLiveNoteLocks(i);
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
	int32_t primary = -1;
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

bool InstrumentClipView::getHeldNoteLockValue(ModelStackWithAutoParam* modelStack, int32_t* value) {
	bool matches[kEditPadPressBufferSize];
	int32_t primary = findHeldNoteLockPresses(modelStack, matches);
	if (primary < 0) {
		return false;
	}
	const note_locks::LiveEdit& live = note_locks::liveEdits[primary];
	if (const int32_t* v = live.armed() ? live.find(modelStack->paramId) : nullptr) {
		*value = *v;
		return true;
	}
	if (const int32_t* v = currentSong->noteLocks.get(getCommittedLockSetForPress(primary)).find(modelStack->paramId)) {
		*value = *v;
		return true;
	}
	*value = modelStack->autoParam->getValuePossiblyAtPos(view.modPos, modelStack);
	return true;
}

bool InstrumentClipView::setHeldNoteLock(ModelStackWithAutoParam* modelStack, int32_t value) {
	bool matches[kEditPadPressBufferSize];
	if (findHeldNoteLockPresses(modelStack, matches) < 0) {
		return false;
	}
	int32_t param = modelStack->paramId;
	bool anyFull = false;
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
		sound->updateLiveNoteLocks(live.tag, param, value, *modelStack->paramManager);
		editPadPresses[i].deleteOnDepress = false;
	}
	if (anyFull) {
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_NOTE_LOCKS_FULL));
	}
	return true;
}
