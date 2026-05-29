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

	// One grouped RECORD action so the whole chord is a single undo step (ActionAddition::ALLOWED
	// coalesces the per-note calls below into this one action).
	Action* action = actionLogger.getNewAction(ActionType::RECORD, ActionAddition::ALLOWED);

	bool committedAny = false;
	for (uint8_t i = 0; i < noteCount; i++) {
		bool scaleAltered = false;
		ModelStackWithNoteRow* modelStackWithNoteRow =
		    clip->getOrCreateNoteRowForYNote(notes[i], modelStack, action, &scaleAltered);
		NoteRow* noteRow = modelStackWithNoteRow->getNoteRowAllowNull();
		if (noteRow != nullptr) {
			// recordNoteOn writes at the live playhead position, quantized; called once per voicing
			// note with no playback advance between, so all notes land at the same position.
			clip->recordNoteOn(modelStackWithNoteRow, selection.velocity);
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
