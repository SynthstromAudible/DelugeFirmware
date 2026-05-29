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

bool ChordService::commit(const ChordSelection& selection, ChordPlacement placement) {
	// Phase 1: only playhead placement is implemented. Step / Pending are future phases.
	if (placement != ChordPlacement::Playhead) {
		return false;
	}

	int16_t notes[kMaxChordKeyboardSize];
	uint8_t noteCount = resolveChordNotes(selection, notes, kMaxChordKeyboardSize);
	if (noteCount == 0) {
		return false;
	}

	InstrumentClip* clip = getCurrentInstrumentClip();
	if (clip == nullptr || clip->output == nullptr || clip->output->type == OutputType::KIT) {
		return false; // melodic clips only for the PoC
	}

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);

	// Place every voicing note at the current playhead position, with a length of one step at the
	// clip's current zoom (the same length a step-pad tap produces). We use attemptNoteAdd rather
	// than recordNoteOn: recordNoteOn lays down a 1-tick note and relies on a later note-off (key
	// release) to set the length, which a one-shot commit never sends — that left committed chords
	// inaudibly short.
	int32_t pos = static_cast<int32_t>(clip->getLivePos());
	int32_t noteLength = static_cast<int32_t>(currentSong->xZoom[NAVIGATION_CLIP]);
	if (noteLength <= 0) {
		noteLength = kDefaultClipLength; // safety fallback
	}

	// One grouped RECORD action so the whole chord is a single undo step (the same action is passed
	// to every attemptNoteAdd below, so their consequences coalesce into one undo).
	Action* action = actionLogger.getNewAction(ActionType::RECORD, ActionAddition::ALLOWED);

	bool committedAny = false;
	for (uint8_t i = 0; i < noteCount; i++) {
		bool scaleAltered = false;
		ModelStackWithNoteRow* modelStackWithNoteRow =
		    clip->getOrCreateNoteRowForYNote(notes[i], modelStack, action, &scaleAltered);
		NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
		if (noteRow != nullptr) {
			int32_t probability = noteRow->getDefaultProbability();
			auto iterance = noteRow->getDefaultIterance();
			int32_t fill = noteRow->getDefaultFill(modelStackWithNoteRow);
			noteRow->attemptNoteAdd(pos, noteLength, selection.velocity, probability, iterance, fill,
			                        modelStackWithNoteRow, action);
			committedAny = true;

			if (action != nullptr && scaleAltered) {
				action->updateYScrollClipViewAfter();
			}
		}
	}

	if (committedAny) {
		display->displayPopup("CHRD");
	}
	return committedAny;
}

} // namespace deluge::gui::ui::keyboard
