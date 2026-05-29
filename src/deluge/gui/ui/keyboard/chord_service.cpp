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
	display->displayPopup("PEND");
}

bool ChordService::hasPending() {
	return hasPending_;
}

void ChordService::clearPending() {
	hasPending_ = false;
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

	// One grouped action shared by every note, so the whole chord is a single undo step.
	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);

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
		display->displayPopup("PLCD");
		hasPending_ = false;
	}
	return placedAny;
}

} // namespace deluge::gui::ui::keyboard
