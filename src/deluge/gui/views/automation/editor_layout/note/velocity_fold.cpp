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

#include "gui/views/automation/editor_layout/note/velocity_fold.h"
#include "gui/l10n/l10n.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/view.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/song/song.h"
#include <algorithm>

using namespace deluge::gui;
using namespace velocity_edit;

namespace {
/// fn(modelStackWithNoteRow, noteRowIndex) for every note row in the clip that has notes
template <typename F>
void forEachNoteRow(InstrumentClip* clip, F&& fn) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = setupModelStackWithTimelineCounter(modelStackMemory, currentSong, clip);
	for (int32_t i = 0; i < clip->noteRows.getNumElements(); i++) {
		NoteRow* noteRow = clip->noteRows.getElement(i);
		if (noteRow->notes.getNumElements()) {
			fn(modelStack->addNoteRow(clip->getNoteRowId(noteRow, i), noteRow), i);
		}
	}
}

/// fn(modelStackWithNoteRow) for every note row that reaches startPos
template <typename F>
void forEachNoteRowReaching(InstrumentClip* clip, int32_t startPos, F&& fn) {
	forEachNoteRow(clip, [&](ModelStackWithNoteRow* modelStackWithNoteRow, int32_t) {
		if (startPos < modelStackWithNoteRow->getLoopLength()) {
			fn(modelStackWithNoteRow);
		}
	});
}

/// the highest velocity of the notes starting on each step in any note row, or 0 if no note starts there.
/// Only steps with notes starting on them can be edited.
void getStepVelocities(InstrumentClip* clip, int32_t stepVelocity[kDisplayWidth]) {
	std::fill(stepVelocity, stepVelocity + kDisplayWidth, 0);
	SquareInfo rowSquareInfo[kDisplayWidth];
	forEachNoteRow(clip, [&](ModelStackWithNoteRow* modelStackWithNoteRow, int32_t) {
		modelStackWithNoteRow->getNoteRow()->getRowSquareInfo(modelStackWithNoteRow->getLoopLength(), rowSquareInfo);
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			uint8_t type = rowSquareInfo[x].squareType;
			if (type == SQUARE_NOTE_HEAD || type == SQUARE_BLURRED) {
				stepVelocity[x] = std::max(stepVelocity[x], rowSquareInfo[x].maxVelocity);
			}
		}
	});
}

int32_t getStepStartPos(int32_t x) {
	return instrumentClipView.getPosFromSquare(x);
}
} // namespace

bool VelocityFold::isFolded() {
	if (getOnArrangerView() || !automationView.inNoteEditor()) {
		return false;
	}
	InstrumentClip* clip = getCurrentInstrumentClip();
	return clip->output->type == OutputType::KIT ? clip->affectEntire : foldedNonKit;
}

void VelocityFold::affectEntireChanged() {
	if (getCurrentOutputType() != OutputType::KIT) {
		foldedNonKit = !foldedNonKit;
	}
	view.setModLedStates();
	display->displayPopup(deluge::l10n::get(isFolded() ? deluge::l10n::String::STRING_FOR_FOLD
	                                                   : deluge::l10n::String::STRING_FOR_UNFOLD));
	renderDisplay();
	uiNeedsRendering(getAutomationView());
}

/// every note row is drawn in its own colour (the same colour it has in the clip view). Each row draws a bar up to its
/// velocity on that step; where bars overlap, the shorter bar wins, so each row shows as a band between its velocity
/// and the next lower row's. Rows with notes starting on a step hide rows that only have a tail there.
void VelocityFold::render(InstrumentClip* clip, RGB image[][kDisplayWidth + kSideBarWidth],
                          uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	constexpr int16_t kNoVelocity = 1000;
	int16_t bestVelocity[kDisplayWidth][kDisplayHeight];
	RGB bestColour[kDisplayWidth][kDisplayHeight];
	bool anyHead[kDisplayWidth] = {};
	for (auto& column : bestVelocity) {
		std::fill(std::begin(column), std::end(column), kNoVelocity);
	}

	bool isKit = clip->output->type == OutputType::KIT;
	SquareInfo rowSquareInfo[kDisplayWidth];

	forEachNoteRow(clip, [&](ModelStackWithNoteRow* modelStackWithNoteRow, int32_t i) {
		NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();
		noteRow->getRowSquareInfo(modelStackWithNoteRow->getLoopLength(), rowSquareInfo);

		// same colours as the clip view (see InstrumentClip::renderAsSingleRow)
		RGB mainColour = clip->getMainColourFromY(isKit ? i : noteRow->y, noteRow->getColourOffset(clip));
		RGB tailColour = mainColour.forTail();
		RGB blurColour = mainColour.forBlur();

		for (int32_t x = 0; x < kDisplayWidth; x++) {
			SquareInfo& square = rowSquareInfo[x];
			if (square.squareType == SQUARE_NO_NOTE) {
				continue;
			}
			bool isTail = square.squareType == SQUARE_NOTE_TAIL;
			if (isTail && anyHead[x]) {
				continue;
			}
			// the first note starting on this step replaces any tails drawn so far
			if (!isTail && !anyHead[x]) {
				anyHead[x] = true;
				std::fill(std::begin(bestVelocity[x]), std::end(bestVelocity[x]), kNoVelocity);
			}
			const RGB& colour = isTail ? tailColour : (square.squareType == SQUARE_NOTE_HEAD) ? mainColour : blurColour;
			for (int32_t y = 0; y < kDisplayHeight && velocityReachesY(square.maxVelocity, y); y++) {
				if (square.maxVelocity < bestVelocity[x][y]) {
					bestVelocity[x][y] = static_cast<int16_t>(square.maxVelocity);
					bestColour[x][y] = colour;
				}
			}
		}
	});

	for (int32_t x = 0; x < kDisplayWidth; x++) {
		for (int32_t y = 0; y < kDisplayHeight; y++) {
			if (bestVelocity[x][y] == kNoVelocity) {
				image[y][x] = colours::black;
			}
			else {
				image[y][x] = bestColour[x][y];
				occupancyMask[y][x] = 64;
			}
		}
	}
}

/// scale every note starting on step x in every note row so the highest one becomes targetVelocity
void VelocityFold::setStepVelocity(InstrumentClip* clip, Action* action, int32_t x, int32_t targetVelocity) {
	int32_t startPos = getStepStartPos(x);
	setRelativeVelocityInRows(action, startPos, getStepStartPos(x + 1), targetVelocity,
	                          [&](auto&& fn) { forEachNoteRowReaching(clip, startPos, fn); });
}

void VelocityFold::endPadPresses() {
	for (auto& press : padPresses) {
		press.isActive = false;
	}
	if (numPadPresses) {
		numPadPresses = 0;
		actionLogger.closeAction(ActionType::NOTE_EDIT);
	}
}

void VelocityFold::padRelease(int32_t x, int32_t y) {
	for (auto& press : padPresses) {
		if (press.isActive && press.xDisplay == x && press.yDisplay == y) {
			if (numPadPresses == 1) {
				endPadPresses();
				renderDisplay();
			}
			else {
				press.isActive = false;
				numPadPresses--;
			}
			return;
		}
	}
}

// folded velocity edit pad action
// 1) single press: set the velocity of every note starting on that step
// 2) two presses in one column: set the velocity to the middle of the two pads
// 3) two presses in different columns: ramp the velocity of every step between them
// empty steps are left alone because there is no single row to add a note to
void VelocityFold::padAction(InstrumentClip* clip, int32_t x, int32_t y, int32_t velocity) {
	if (!velocity) {
		padRelease(x, y);
		return;
	}

	int32_t stepVelocity[kDisplayWidth];
	getStepVelocities(clip, stepVelocity);
	if (!stepVelocity[x]) {
		return;
	}

	PadPress* newPress = std::ranges::find_if_not(padPresses, &PadPress::isActive);
	if (newPress == std::end(padPresses)) {
		return;
	}
	// a single press already held combines with this one
	PadPress* firstPress = (numPadPresses == 1) ? std::ranges::find_if(padPresses, &PadPress::isActive) : nullptr;
	*newPress = {true, x, y, getStepStartPos(x), getStepStartPos(x + 1)};
	numPadPresses++;

	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
	if (!action) {
		return;
	}

	uiNeedsRendering(getAutomationView(), 0xFFFFFFFF, 0);

	// two presses in different columns: ramp
	if (firstPress && firstPress->xDisplay != x) {
		ramp.setup(firstPress->xDisplay, stepVelocity[firstPress->xDisplay], x, y);
		ramp.forEachStep([&](int32_t i) { return stepVelocity[i] > 0; },
		                 [&](int32_t i, int32_t rampVelocity) { setStepVelocity(clip, action, i, rampVelocity); });
		ramp.renderValues(x);
		return;
	}

	int32_t newVelocity = getVelocityFromY(y);
	// two presses in the same column: middle velocity
	if (firstPress) {
		newVelocity = (newVelocity + getVelocityFromY(firstPress->yDisplay)) / 2;
	}
	setStepVelocity(clip, action, x, newVelocity);
	renderDisplay(newVelocity);
}

void VelocityFold::adjustHeldVelocity(InstrumentClip* clip, int32_t offset) {
	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
	if (!action) {
		return;
	}
	int32_t displayVelocity = 0;
	for (auto& press : padPresses) {
		if (!press.isActive) {
			continue;
		}
		forEachNoteRowReaching(clip, press.startPos, [&](ModelStackWithNoteRow* modelStackWithNoteRow) {
			NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();
			forNotesInRange(noteRow, press.startPos, press.endPos, [&](Note* note) {
				noteRow->changeNotesAcrossAllScreens(note->pos, modelStackWithNoteRow, action,
				                                     CORRESPONDING_NOTES_ADJUST_VELOCITY, offset);
				displayVelocity = std::max<int32_t>(displayVelocity, note->getVelocity());
			});
		});
	}
	uiNeedsRendering(getAutomationView(), 0xFFFFFFFF, 0);
	if (displayVelocity) {
		renderDisplay(displayVelocity);
	}
}
