/*
 * Copyright © 2016-2025 Synthstrom Audible Limited
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

#include "gui/ui/keyboard/chord_service.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/l10n/strings.h"
#include "hid/display/display.h"
#include "model/action/action.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/output.h"
#include "model/song/song.h"

namespace deluge::gui::ui::keyboard {

namespace {
// Lives here (module state, not on a UI) so the pending chord survives the keyboard-view ->
// clip-view switch.
PendingChord pendingChord_;
bool hasPending_ = false;
} // namespace

void ChordService::capturePending(const PendingChord& chord) {
	if (chord.count == 0) {
		return;
	}
	pendingChord_ = chord;
	hasPending_ = true;
	display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CHORD_BRUSH_ARMED));
}

bool ChordService::hasPending() {
	return hasPending_;
}

void ChordService::clearPending() {
	if (!hasPending_) {
		return;
	}
	hasPending_ = false;
	display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CHORD_BRUSH_CLEARED));
}

uint8_t ChordService::getPendingNotes(int16_t* notesOut, uint8_t maxNotes, uint8_t* velocityOut) {
	if (!hasPending_) {
		return 0;
	}
	uint8_t count = pendingChord_.count < maxNotes ? pendingChord_.count : maxNotes;
	for (uint8_t i = 0; i < count; i++) {
		notesOut[i] = pendingChord_.notes[i];
	}
	if (velocityOut != nullptr) {
		*velocityOut = pendingChord_.velocity;
	}
	return count;
}

void ChordService::voicingCycle(int8_t steps) {
	if (!hasPending_ || pendingChord_.count == 0) {
		return;
	}
	int8_t direction = steps > 0 ? 1 : -1;
	int8_t count = steps > 0 ? steps : -steps;
	for (int8_t s = 0; s < count; s++) {
		// Find the lowest and highest note in the resolved payload.
		uint8_t loIdx = 0;
		uint8_t hiIdx = 0;
		for (uint8_t i = 1; i < pendingChord_.count; i++) {
			if (pendingChord_.notes[i] < pendingChord_.notes[loIdx]) {
				loIdx = i;
			}
			if (pendingChord_.notes[i] > pendingChord_.notes[hiIdx]) {
				hiIdx = i;
			}
		}
		if (direction > 0) {
			// Clockwise: move the lowest note up an octave (clamped to MIDI range).
			if (pendingChord_.notes[loIdx] + 12 <= 127) {
				pendingChord_.notes[loIdx] += 12;
			}
		}
		else {
			// Counterclockwise: move the highest note down an octave (clamped to MIDI range).
			if (pendingChord_.notes[hiIdx] - 12 >= 0) {
				pendingChord_.notes[hiIdx] -= 12;
			}
		}
	}
}

bool ChordService::placePendingAt(int32_t pos, int32_t length) {
	if (!hasPending_ || pendingChord_.count == 0) {
		return false;
	}

	InstrumentClip* clip = getCurrentInstrumentClip();
	if (clip == nullptr || clip->output == nullptr || clip->output->type == OutputType::KIT) {
		return false; // melodic clips only
	}
	if (length <= 0) {
		length = kDefaultClipLength; // safety fallback
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// A fresh action per placement (NOT_ALLOWED = don't merge with the previous one), shared by every
	// note in THIS chord. So one chord = one undo step, and each stamped chord undoes independently.
	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::NOT_ALLOWED);

	bool placedAny = false;
	for (uint8_t i = 0; i < pendingChord_.count; i++) {
		bool scaleAltered = false;
		ModelStackWithNoteRow* modelStackWithNoteRow =
		    clip->getOrCreateNoteRowForYNote(pendingChord_.notes[i], modelStack, action, &scaleAltered);
		NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
		if (noteRow != nullptr) {
			// Explicit-position write at the tapped step, with an explicit length — the chord lands
			// vertically aligned (all notes share pos + length).
			int32_t probability = noteRow->getDefaultProbability();
			auto iterance = noteRow->getDefaultIterance();
			int32_t fill = noteRow->getDefaultFill(modelStackWithNoteRow);
			noteRow->attemptNoteAdd(pos, length, pendingChord_.velocity, probability, iterance, fill,
			                        modelStackWithNoteRow, action);
			placedAny = true;

			if (action != nullptr && scaleAltered) {
				action->updateYScrollClipViewAfter();
			}
		}
	}

	if (placedAny) {
		// Keep the pending chord so it can be stamped on multiple steps (e.g. syncopated rhythms).
		// It is cleared when the user leaves the piano-roll screen (see changeRootUI).
		display->displayPopup(deluge::l10n::get(deluge::l10n::String::STRING_FOR_CHORD_BRUSH_PLACED));
	}
	return placedAny;
}

} // namespace deluge::gui::ui::keyboard
