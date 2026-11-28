/*
 * Copyright © 2026 Sean Ditny
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

#include "gui/views/note_clipboard.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/note_edit_math.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/consequence/consequence_note_array_change.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"
#include "util/finally.h"
#include <algorithm>
#include <new>

namespace deluge::gui {
NoteClipboard noteClipboard;

namespace {
// getNoteRowFromId can create melodic rows; previews and source validation must not.
NoteRow* findRow(InstrumentClip* clip, int32_t id) {
	if (clip->output->type == OutputType::KIT) {
		return id >= 0 && id < clip->getNumNoteRows() ? clip->noteRows.getElement(id) : nullptr;
	}
	return clip->getNoteRowForYNote(id);
}

bool sameNote(const Note& a, const Note& b) {
	return a.pos == b.pos && a.length == b.length && a.velocity == b.velocity && a.lift == b.lift
	       && a.probability == b.probability && a.iterance == b.iterance && a.fill == b.fill;
}

// All allocations and note-array edits are staged before the first live row changes.
struct RowChange {
	int32_t id;
	NoteVector notes;
	ConsequenceNoteArrayChange* undo = nullptr;
	RowChange* next = nullptr;
	~RowChange() {
		if (undo) {
			undo->~ConsequenceNoteArrayChange();
			delugeDealloc(undo);
		}
	}
};
} // namespace

NoteClipboard::~NoteClipboard() {
	clear();
}

void NoteClipboard::clear() {
	while (entries_) {
		auto* next = entries_->next;
		entries_->~Entry();
		delugeDealloc(entries_);
		entries_ = next;
	}
	cut_ = false;
	preview_ = false;
	placing_ = false;
	sourceClip_ = nullptr;
	sourceSong_ = nullptr;
	previewClip_ = nullptr;
}

void NoteClipboard::clearClipboard() {
	clear(); // Also discards a pending cut and exits paste placement without deleting source notes.
	learnCopyStarted_ = false;
	notePulseOff_ = false;
	uiTimerManager.unsetTimer(TimerName::NOTE_CLIPBOARD_BLINK);
	// Keep consumed pad releases suppressed, even if a preview pad is still held.
	uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
	renderUIsForOled();
	display->displayPopup("Clipboard cleared");
}

uint8_t NoteClipboard::iconNoteCount() const {
	uint8_t count = 0;
	for (auto* entry = entries_; entry && count < 6; entry = entry->next) {
		++count;
	}
	return count;
}

bool NoteClipboard::clipboardNoteVisible() {
	if (!hasNotes()) {
		notePulseOff_ = false;
		return false;
	}
	if (!uiTimerManager.isTimerSet(TimerName::NOTE_CLIPBOARD_BLINK)) {
		uiTimerManager.setTimer(TimerName::NOTE_CLIPBOARD_BLINK, notePulseOff_ ? 200 : 2400);
	}
	return !notePulseOff_;
}

void NoteClipboard::blinkClipboardNote() {
	if (getCurrentUI() != &soundEditor || !soundEditor.inNoteEditor()) {
		notePulseOff_ = false;
		return;
	}
	notePulseOff_ = hasNotes() && !notePulseOff_;
	// Rendering the clipboard icons schedules the next tick. Leaving their page
	// stops the animation automatically, without redrawing unrelated screens.
	renderUIsForOled();
}

bool NoteClipboard::appendNote(const Note& note, int32_t rowId, int32_t visual) {
	for (auto* entry = entries_; entry; entry = entry->next) {
		if (entry->rowId == rowId && entry->note.pos == note.pos) {
			return true;
		}
	}
	void* memory = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(Entry));
	if (!memory) {
		display->displayError(Error::INSUFFICIENT_RAM);
		return false;
	}
	auto* entry = new (memory) Entry{note, rowId, visual, entries_};
	if (!entries_) {
		originPos_ = note.pos;
		originVisual_ = visual;
	}
	else {
		originPos_ = std::min(originPos_, note.pos);
		// Grid rows increase upward: the highest row is the top of the copied shape.
		originVisual_ = std::max(originVisual_, visual);
	}
	entries_ = entry;
	return true;
}

bool NoteClipboard::appendTarget(const EditPadPress& target) {
	char memory[MODEL_STACK_MAX_SIZE];
	auto* stack = currentSong->setupModelStackWithCurrentClip(memory);
	auto* rowStack = instrumentClipView.get_note_edit_row(stack, target);
	auto* row = rowStack->getNoteRowAllowNull();
	if (!row) {
		return true;
	}
	auto* clip = getCurrentInstrumentClip();
	int32_t visual =
	    kit_ ? rowStack->noteRowId
	         : clip->getYVisualFromYNote(clip->getDisplayAnchorYNote(rowStack->noteRowId, currentSong), currentSong);
	for (int32_t i = row->notes.search(target.intendedPos, GREATER_OR_EQUAL); i < row->notes.getNumElements(); ++i) {
		auto* note = row->notes.getElement(i);
		if (target.isBlurredSquare ? note->pos - target.intendedPos >= target.intendedLength
		                           : note->pos != target.intendedPos) {
			break;
		}
		if (!appendNote(*note, rowStack->noteRowId, visual)) {
			return false;
		}
	}
	return true;
}

bool NoteClipboard::captureSelection() {
	sourceClip_ = getCurrentInstrumentClip();
	sourceSong_ = currentSong;
	kit_ = sourceClip_->output->type == OutputType::KIT;
	for (const auto& target : instrumentClipView.get_note_edit_targets()) {
		if (target.isActive && !appendTarget(target)) {
			clear();
			return false;
		}
	}
	if (!entries_) {
		display->displayPopup("Select Note");
		return false;
	}
	return true;
}

bool NoteClipboard::copySelection(bool cut) {
	NoteClipboard captured;
	if (!captured.captureSelection()) {
		return false;
	}
	clear();
	std::swap(entries_, captured.entries_);
	sourceClip_ = captured.sourceClip_;
	sourceSong_ = captured.sourceSong_;
	kit_ = captured.kit_;
	originPos_ = captured.originPos_;
	originVisual_ = captured.originVisual_;
	cut_ = cut;
	placing_ = true;
	instrumentClipView.dontDeleteNotesOnDepress();
	uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
	renderUIsForOled();
	display->displayPopup(cut ? "Notes cut" : l10n::get(l10n::String::STRING_FOR_NOTES_COPIED));
	return true;
}

bool NoteClipboard::isSourceNote(int32_t rowId, int32_t pos) const {
	for (auto* entry = entries_; entry; entry = entry->next) {
		if (entry->rowId == rowId && entry->note.pos == pos) {
			return true;
		}
	}
	return false;
}

bool NoteClipboard::sourceStillMatches() {
	if (sourceSong_ != currentSong || sourceClip_ != getCurrentInstrumentClip()) {
		return false;
	}
	for (auto* entry = entries_; entry; entry = entry->next) {
		auto* row = findRow(sourceClip_, entry->rowId);
		auto* note = row ? row->notes.getElement(row->notes.search(entry->note.pos, GREATER_OR_EQUAL)) : nullptr;
		if (!note || !sameNote(*note, entry->note)) {
			return false;
		}
	}
	return true;
}

bool NoteClipboard::destination(const Entry& entry, int32_t& rowId, int32_t& pos) {
	auto* clip = getCurrentInstrumentClip();
	int64_t position = static_cast<int64_t>(pastePos_) + entry.note.pos - originPos_;
	if (position < 0 || position > kMaxSequenceLength) {
		return false;
	}
	pos = position;
	int32_t visual = pasteVisual_ + entry.visual - originVisual_;
	if (kit_) {
		rowId = visual;
		return rowId >= 0 && rowId < clip->getNumNoteRows();
	}
	rowId = clip->getYNoteFromYDisplay(visual - clip->yScroll, currentSong);
	return clip->isScrollWithinRange(0, rowId);
}

int32_t NoteClipboard::pastedLength(const Entry& entry, int32_t rowId, int32_t pos) {
	if (firmly_) {
		return entry.note.length;
	}
	auto* clip = getCurrentInstrumentClip();
	auto* row = findRow(clip, rowId);
	if (!row) {
		return entry.note.length;
	}
	char memory[MODEL_STACK_MAX_SIZE];
	auto* stack = currentSong->setupModelStackWithCurrentClip(memory);
	int32_t loopLength = stack->addNoteRow(rowId, row)->getLoopLength();
	int32_t length = entry.note.length;
	for (int32_t i = 0; i < row->notes.getNumElements(); ++i) {
		auto* other = row->notes.getElement(i);
		if (cut_ && isSourceNote(rowId, other->pos)) {
			continue;
		}
		length = gentleNotePasteLength(pos, length, other->pos, other->length, loopLength);
		if (!length) {
			break;
		}
	}
	return length;
}

bool NoteClipboard::canPaste() {
	auto* clip = getCurrentInstrumentClip();
	if (!entries_ || !preview_ || previewClip_ != clip || kit_ != (clip->output->type == OutputType::KIT)
	    || (cut_ && !sourceStillMatches())) {
		return false;
	}
	char memory[MODEL_STACK_MAX_SIZE];
	auto* stack = currentSong->setupModelStackWithCurrentClip(memory);
	bool anyFits = false;
	for (auto* entry = entries_; entry; entry = entry->next) {
		int32_t rowId, pos;
		if (!destination(*entry, rowId, pos)) {
			return false;
		}
		auto* row = findRow(clip, rowId);
		int32_t length = row ? stack->addNoteRow(rowId, row)->getLoopLength() : clip->loopLength;
		if (!notePasteFits(pos, entry->note.length, length)) {
			return false;
		}
		int32_t addedLength = pastedLength(*entry, rowId, pos);
		// A cut must move the complete selection, never discard a blocked source note.
		if (cut_ && addedLength != entry->note.length) {
			return false;
		}
		anyFits |= addedLength > 0;
	}
	return anyFits;
}

bool NoteClipboard::previewAt(int32_t x, int32_t y) {
	if (!entries_) {
		display->displayPopup("Copy notes first");
		return false;
	}
	pastePos_ = instrumentClipView.getPosFromSquare(x);
	pasteVisual_ = getCurrentInstrumentClip()->yScroll + y;
	previewClip_ = getCurrentInstrumentClip();
	preview_ = true;
	placing_ = true;
	consumedPads_[y] |= 1u << x;
	bool valid = canPaste();
	uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
	if (!valid) {
		display->displayPopup("Cannot paste here");
	}
	return valid;
}

void NoteClipboard::cancelPreview() {
	preview_ = false;
	previewClip_ = nullptr;
	uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
}

void NoteClipboard::exitEditor() {
	placing_ = false;
	cancelPreview();
	// A cut is a pending move, scoped to this editor session and source clip.
	if (cut_) {
		clear();
	}
}

bool NoteClipboard::commit(bool deleteOnly) {
	if (deleteOnly ? !sourceStillMatches() : !canPaste()) {
		display->displayPopup(deleteOnly ? "Selection changed" : "Cannot paste here");
		return false;
	}
	auto* clip = getCurrentInstrumentClip();
	RowChange* changes = nullptr;
	auto cleanup = util::finally([&] {
		while (changes) {
			auto* next = changes->next;
			changes->~RowChange();
			delugeDealloc(changes);
			changes = next;
		}
	});
	char memory[MODEL_STACK_MAX_SIZE];
	auto* stack = currentSong->setupModelStackWithCurrentClip(memory);
	auto stage = [&](int32_t id) -> RowChange* {
		for (auto* change = changes; change; change = change->next) {
			if (change->id == id) {
				return change;
			}
		}
		auto* row = findRow(clip, id);
		if (!row && !kit_) {
			row = clip->getOrCreateNoteRowForYNote(id, stack)->getNoteRowAllowNull();
		}
		if (!row) {
			return nullptr;
		}
		void* storage = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(RowChange));
		if (!storage) {
			return nullptr;
		}
		auto* change = new (storage) RowChange;
		change->id = id;
		change->next = changes;
		changes = change;
		if (!change->notes.cloneFrom(&row->notes)) {
			return nullptr;
		}
		storage = GeneralMemoryAllocator::get().allocLowSpeed(sizeof(ConsequenceNoteArrayChange));
		if (!storage) {
			return nullptr;
		}
		NoteVector empty;
		change->undo = new (storage) ConsequenceNoteArrayChange(clip, id, &empty, true);
		return change;
	};
	auto ramError = [] {
		display->displayError(Error::INSUFFICIENT_RAM);
		return false;
	};
	if (cut_ || deleteOnly) {
		for (auto* entry = entries_; entry; entry = entry->next) {
			auto* change = stage(entry->rowId);
			if (!change) {
				return ramError();
			}
			change->notes.deleteAtKey(entry->note.pos);
		}
	}
	if (!deleteOnly && firmly_) {
		int64_t end = pastePos_;
		int32_t bottom = pasteVisual_;
		for (auto* entry = entries_; entry; entry = entry->next) {
			end = std::max(end, static_cast<int64_t>(pastePos_) + entry->note.pos - originPos_ + entry->note.length);
			bottom = std::min(bottom, pasteVisual_ + entry->visual - originVisual_);
		}
		for (int32_t visual = bottom; visual <= pasteVisual_; ++visual) {
			int32_t id = kit_ ? visual : clip->getYNoteFromYDisplay(visual - clip->yScroll, currentSong);
			auto* row = findRow(clip, id);
			if (!row) {
				continue;
			}
			int32_t length = stack->addNoteRow(id, row)->getLoopLength();
			auto* change = stage(id);
			if (!change) {
				return ramError();
			}
			for (int32_t i = change->notes.getNumElements() - 1; i >= 0; --i) {
				auto* note = change->notes.getElement(i);
				int32_t remaining = noteLengthAfterPasteClear(note->pos, note->length, length, pastePos_, end);
				if (!remaining) {
					change->notes.deleteAtIndex(i);
				}
				else {
					note->length = remaining;
				}
			}
		}
	}
	if (!deleteOnly) {
		for (auto* entry = entries_; entry; entry = entry->next) {
			int32_t rowId, pos;
			if (!destination(*entry, rowId, pos)) {
				return false;
			}
			int32_t length = pastedLength(*entry, rowId, pos);
			if (!length) {
				continue;
			}
			auto* change = stage(rowId);
			if (!change) {
				return ramError();
			}
			int32_t index = change->notes.insertAtKey(pos);
			if (index < 0) {
				return ramError();
			}
			auto* note = change->notes.getElement(index);
			*note = entry->note;
			note->pos = pos;
			note->length = length;
		}
	}
	Action* action = actionLogger.getNewAction(deleteOnly ? ActionType::NOTE_EDIT : ActionType::NOTES_PASTE,
	                                           ActionAddition::NOT_ALLOWED);
	if (!action) {
		return ramError();
	}
	// No operation below allocates memory or can fail: undo owns every original row.
	for (auto* change = changes; change; change = change->next) {
		// Creating another melodic row can relocate the note-row array; resolve by ID again.
		findRow(clip, change->id)->notes.swapStateWith(&change->notes);
		change->undo->backedUpNoteVector.swapStateWith(&change->notes);
		action->addConsequence(change->undo);
		change->undo = nullptr;
	}
	finishEdit();
	return true;
}

void NoteClipboard::finishEdit() {
	instrumentClipView.dontDeleteNotesOnDepress();
	instrumentClipView.endAllEditPadPresses();
	instrumentClipView.clearRememberedNotes();
	instrumentClipView.stopSelectedNotePulsing();
	instrumentClipView.reassessAllAuditionStatus();
	getCurrentInstrumentClip()->expectEvent();
	uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0xFFFFFFFF);
	if (getCurrentUI() == &soundEditor) {
		soundEditor.getCurrentMenuItem()->readValueAgain();
	}
}

void NoteClipboard::setPasteMode(bool firmly) {
	firmly_ = firmly;
	placing_ = hasNotes();
	uiNeedsRendering(&instrumentClipView, 0xFFFFFFFF, 0);
}

bool NoteClipboard::paste(bool firmly) {
	setPasteMode(firmly);
	if (!commit(false)) {
		return false;
	}
	if (cut_) {
		clear(); // A successful cut is consumed; it cannot delete the old sources twice.
	}
	else {
		cancelPreview();
	}
	placing_ = false;
	display->displayPopup(l10n::get(l10n::String::STRING_FOR_NOTES_PASTED));
	return true;
}

bool NoteClipboard::deleteSelection() {
	NoteClipboard selected;
	if (!selected.captureSelection() || !selected.commit(true)) {
		return false;
	}
	exitEditor();
	display->displayPopup("Notes deleted");
	return true;
}

void NoteClipboard::learnButton(bool) {
	// Each Learn hold starts a fresh copy gesture; only subsequent pad presses copy notes.
	learnCopyStarted_ = false;
}

bool NoteClipboard::copySquare(int32_t x, int32_t y) {
	auto* clip = getCurrentInstrumentClip();
	char memory[MODEL_STACK_MAX_SIZE];
	auto* stack = currentSong->setupModelStackWithCurrentClip(memory);
	auto* rowStack = clip->getNoteRowOnScreen(y, stack);
	auto* row = rowStack->getNoteRowAllowNull();
	if (!row) {
		return false;
	}
	SquareInfo square;
	row->getSquareInfo(x, rowStack->getLoopLength(), square);
	if (!square.numNotes) {
		return false;
	}
	if (!learnCopyStarted_ || sourceClip_ != clip || sourceSong_ != currentSong) {
		clear();
		sourceClip_ = clip;
		sourceSong_ = currentSong;
		kit_ = clip->output->type == OutputType::KIT;
	}
	if (square.squareType == SQUARE_NOTE_TAIL && square.firstNote) {
		if (!appendNote(*square.firstNote, rowStack->noteRowId, clip->yScroll + y)) {
			clear();
			learnCopyStarted_ = false;
			return false;
		}
		learnCopyStarted_ = true;
		renderUIsForOled();
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_NOTES_COPIED));
		return true;
	}
	int32_t start = instrumentClipView.getPosFromSquare(x);
	int32_t end = start + instrumentClipView.getSquareWidth(x, rowStack->getLoopLength());
	int32_t visual = clip->yScroll + y;
	for (int32_t i = row->notes.search(start, GREATER_OR_EQUAL); i < row->notes.getNumElements(); ++i) {
		auto* note = row->notes.getElement(i);
		if (note->pos >= end) {
			break;
		}
		if (!appendNote(*note, rowStack->noteRowId, visual)) {
			clear();
			learnCopyStarted_ = false;
			return false;
		}
	}
	learnCopyStarted_ = hasNotes();
	if (learnCopyStarted_) {
		renderUIsForOled();
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_NOTES_COPIED));
	}
	return learnCopyStarted_;
}

bool NoteClipboard::consumePadRelease(int32_t x, int32_t y, int32_t velocity) {
	if (!velocity && (consumedPads_[y] & (1u << x))) {
		consumedPads_[y] &= ~(1u << x);
		return true;
	}
	return false;
}

bool NoteClipboard::shortcutPadAction(int32_t x, int32_t y, int32_t velocity) {
	if (consumePadRelease(x, y, velocity)) {
		return true;
	}
	if (!velocity
	    || (getCurrentUI() != &instrumentClipView && !(getCurrentUI() == &soundEditor && soundEditor.inNoteEditor()))) {
		return false;
	}
	bool learn = Buttons::isButtonPressed(hid::button::LEARN);
	bool gentle = Buttons::isShiftButtonPressed() && Buttons::isButtonPressed(hid::button::CROSS_SCREEN_EDIT);
	if (!learn && !gentle) {
		return false;
	}
	consumedPads_[y] |= 1u << x;
	if (Buttons::isShiftButtonPressed()) {
		if (gentle) {
			Buttons::considerCrossScreenReleaseForCrossScreenMode = false;
		}
		setPasteMode(learn);
		if (previewAt(x, y)) {
			paste(learn);
		}
		cancelPreview();
		placing_ = false;
	}
	else {
		copySquare(x, y);
	}
	return true;
}

void NoteClipboard::renderPreview(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth]) {
	if (getCurrentUI() != &soundEditor || !soundEditor.inNoteEditor() || !canPaste()) {
		return;
	}
	auto* clip = getCurrentInstrumentClip();
	for (auto* entry = entries_; entry; entry = entry->next) {
		int32_t rowId, pos;
		if (!destination(*entry, rowId, pos)) {
			continue;
		}
		int32_t length = pastedLength(*entry, rowId, pos);
		if (!length) {
			continue;
		}
		int32_t y = (kit_ ? rowId : clip->getYVisualFromYNote(rowId, currentSong)) - clip->yScroll;
		if (y < 0 || y >= kDisplayHeight || !(whichRows & (1u << y))) {
			continue;
		}
		int32_t first = instrumentClipView.getSquareFromPos(pos);
		int32_t last = instrumentClipView.getSquareFromPos(pos + length - 1);
		for (int32_t x = std::max<int32_t>(0, first); x <= std::min<int32_t>(last, kDisplayWidth - 1); ++x) {
			image[y][x] = x == first ? RGB{70, 70, 70} : RGB{25, 25, 25};
		}
	}
}
} // namespace deluge::gui
