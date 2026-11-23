/*
 * Copyright (c) 2023 Sean Ditny
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

#include "gui/views/automation/editor_layout/note/velocity.h"
#include "gui/views/instrument_clip_view.h"
#include "model/action/action_logger.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/note/note.h"
#include "model/note/note_row.h"

// namespace deluge::gui::views::automation::editor_layout::note {

using namespace deluge::gui;

// colours for the velocity editor

const RGB velocityRowColour[kDisplayHeight] = {{0, 0, 255},   {36, 0, 219}, {73, 0, 182}, {109, 0, 146},
                                               {146, 0, 109}, {182, 0, 73}, {219, 0, 36}, {255, 0, 0}};

const RGB velocityRowTailColour[kDisplayHeight] = {{2, 2, 53},  {9, 2, 46},  {17, 2, 38}, {24, 2, 31},
                                                   {31, 2, 24}, {38, 2, 17}, {46, 2, 9},  {53, 2, 2}};

const RGB velocityRowBlurColour[kDisplayHeight] = {{71, 71, 111}, {72, 66, 101}, {73, 62, 90}, {74, 57, 80},
                                                   {76, 53, 70},  {77, 48, 60},  {78, 44, 49}, {79, 39, 39}};

// lookup tables for the values that are set when you press the pads in each row of the grid
const int32_t padPressValues[kDisplayHeight] = {0, 18, 37, 55, 73, 91, 110, 128};

// lookup tables for the min value of each pad's value range used to display automation on each row of the grid
const int32_t minPadDisplayValues[kDisplayHeight] = {0, 17, 33, 49, 65, 81, 97, 113};

// lookup tables for the max value of each pad's value range used to display automation on each row of the grid
const int32_t maxPadDisplayValues[kDisplayHeight] = {16, 32, 48, 64, 80, 96, 112, 128};

// summary of pad ranges and press values (format: MIN < PRESS < MAX)
// y = 7 :: 113 < 128 < 128
// y = 6 ::  97 < 110 < 112
// y = 5 ::  81 <  91 <  96
// y = 4 ::  65 <  73 <  80
// y = 3 ::  49 <  55 <  64
// y = 2 ::  33 <  37 <  48
// y = 1 ::  17 <  18 <  32
// y = 0 ::  0  <   0 <  16

PLACE_SDRAM_BSS AutomationEditorLayoutNoteVelocity automationEditorLayoutNoteVelocity{};

// relative velocity scaling
// When a press edits several notes at once, the highest velocity note goes to the velocity of the pad pressed and the
// others are scaled with it so they keep the same ratio to it. Squares with several notes are drawn up to their
// highest velocity, so the column ends up at the pressed pad.

namespace {
int32_t scaleVelocity(int32_t velocity, float scale) {
	return std::clamp<int32_t>(static_cast<int32_t>(std::lround(static_cast<float>(velocity) * scale)), 1, 127);
}

/// the scale which puts the highest velocity note (maxVelocity) exactly on targetVelocity. Every other note is scaled
/// by the same amount so they keep their ratio to it
float getRelativeVelocityScale(int32_t maxVelocity, int32_t targetVelocity) {
	return maxVelocity ? static_cast<float>(targetVelocity) / static_cast<float>(maxVelocity) : 1.f;
}

/// fn(note) for every note of noteRow starting in [startPos, endPos)
template <typename F>
void forNotesInRange(NoteRow* noteRow, int32_t startPos, int32_t endPos, F&& fn) {
	int32_t noteI = noteRow->notes.search(startPos, GREATER_OR_EQUAL);
	for (Note* note = noteRow->notes.getElement(noteI); note && note->pos < endPos;
	     note = noteRow->notes.getElement(++noteI)) {
		fn(note);
	}
}

/// fn(x, velocity) for each step between leftX and rightX that isStep(x) accepts, with the velocity interpolated
/// linearly from leftVelocity on the first step to rightVelocity on the last
template <typename IsStep, typename F>
void forEachRampStep(int32_t leftX, int32_t rightX, int32_t leftVelocity, int32_t rightVelocity, IsStep&& isStep,
                     F&& fn) {
	int32_t steps[kDisplayWidth];
	int32_t numSteps = 0;
	for (int32_t x = leftX; x <= rightX; x++) {
		if (isStep(x)) {
			steps[numSteps++] = x;
		}
	}
	for (int32_t step = 0; step < numSteps; step++) {
		int32_t velocity = leftVelocity;
		if (numSteps > 1) {
			float fraction = static_cast<float>(step) / static_cast<float>(numSteps - 1);
			velocity += static_cast<int32_t>(std::round((rightVelocity - leftVelocity) * fraction));
		}
		fn(steps[step], std::clamp<int32_t>(velocity, 1, 127));
	}
}

/// scale the velocities of the notes in [startPos, endPos) of one note row so the highest one becomes
/// targetVelocity. Calls onNoteChanged(note) for each note after changing it.
template <typename F>
void setRelativeVelocity(NoteRow* noteRow, ModelStackWithNoteRow* modelStackWithNoteRow, Action* action,
                         int32_t startPos, int32_t endPos, int32_t targetVelocity, F&& onNoteChanged) {
	int32_t maxVelocity = 0;
	forNotesInRange(noteRow, startPos, endPos,
	                [&](Note* note) { maxVelocity = std::max<int32_t>(maxVelocity, note->getVelocity()); });
	float scale = getRelativeVelocityScale(maxVelocity, targetVelocity);

	forNotesInRange(noteRow, startPos, endPos, [&](Note* note) {
		noteRow->changeNotesAcrossAllScreens(note->pos, modelStackWithNoteRow, action, CORRESPONDING_NOTES_SET_VELOCITY,
		                                     scaleVelocity(note->getVelocity(), scale));
		onNoteChanged(note);
	});
}
} // namespace

/// render each square in each column of the note editor grid
void AutomationEditorLayoutNoteVelocity::renderNoteColumn(ModelStackWithNoteRow* modelStackWithNoteRow,
                                                          InstrumentClip* clip,
                                                          RGB image[][kDisplayWidth + kSideBarWidth],
                                                          uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                                          int32_t xDisplay, int32_t xScroll, int32_t xZoom,
                                                          SquareInfo& squareInfo) {
	// iterate through each square
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		renderNoteSquare(image, occupancyMask, xDisplay, yDisplay, squareInfo.squareType, squareInfo.maxVelocity);
	}
}

/// render column for note parameter
void AutomationEditorLayoutNoteVelocity::renderNoteSquare(RGB image[][kDisplayWidth + kSideBarWidth],
                                                          uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                                          int32_t xDisplay, int32_t yDisplay, uint8_t squareType,
                                                          int32_t value) {
	RGB& pixel = image[yDisplay][xDisplay];
	bool doRender = false;

	if (squareType == SQUARE_NO_NOTE) {
		pixel = colours::black; // erase pad
	}
	else {
		// render square
		if (value >= minPadDisplayValues[yDisplay]) {
			doRender = true;
			if (squareType == SQUARE_NOTE_HEAD) {
				pixel = velocityRowColour[yDisplay];
			}
			else if (squareType == SQUARE_NOTE_TAIL) {
				pixel = velocityRowTailColour[yDisplay];
			}
			else if (squareType == SQUARE_BLURRED) {
				pixel = velocityRowBlurColour[yDisplay];
			}
			occupancyMask[yDisplay][xDisplay] = 64;
		}
		else {
			pixel = colours::black; // erase pad
		}
	}
}

/// updates OLED display to display the current velocity
void AutomationEditorLayoutNoteVelocity::displayParameterValueOLED(deluge::hid::display::oled_canvas::Canvas& canvas,
                                                                   int32_t yPos, int32_t knobPosLeft,
                                                                   int32_t knobPosRight) {
	if (knobPosRight != kNoSelection) {
		char bufferLeft[10];
		bufferLeft[0] = 'L';
		bufferLeft[1] = ':';
		bufferLeft[2] = ' ';
		intToString(knobPosLeft, &bufferLeft[3]);
		canvas.drawString(bufferLeft, 0, yPos, kTextSpacingX, kTextSpacingY);

		char bufferRight[10];
		bufferRight[0] = 'R';
		bufferRight[1] = ':';
		bufferRight[2] = ' ';
		intToString(knobPosRight, &bufferRight[3]);
		canvas.drawStringAlignRight(bufferRight, yPos, kTextSpacingX, kTextSpacingY);
	}
	else if (knobPosLeft != kNoSelection) {
		char buffer[5];
		intToString(knobPosLeft, buffer);
		canvas.drawStringCentred(buffer, yPos, kTextSpacingX, kTextSpacingY);
	}
	else {
		char buffer[5];
		intToString(getCurrentInstrument()->defaultVelocity, buffer);
		canvas.drawStringCentred(buffer, yPos, kTextSpacingX, kTextSpacingY);
	}
}

// velocity edit pad action
void AutomationEditorLayoutNoteVelocity::velocityEditPadAction(ModelStackWithNoteRow* modelStackWithNoteRow,
                                                               NoteRow* noteRow, InstrumentClip* clip, int32_t x,
                                                               int32_t y, int32_t velocity, int32_t effectiveLength,
                                                               SquareInfo& squareInfo) {
	// save pad selected
	getLeftPadSelectedX() = x;

	// calculate new velocity based on Y of pad pressed
	int32_t newVelocity = getVelocityFromY(y);

	// middle pad press variables
	getMiddlePadPressSelected() = false;

	// multi pad press variables
	getMultiPadPressSelected() = false;
	SquareInfo rowSquareInfo[kDisplayWidth];

	// update velocity editor rendering
	bool refreshVelocityEditor = false;
	bool showNewVelocity = true;

	// check for middle or multi pad press
	if (velocity && squareInfo.numNotes != 0 && instrumentClipView.numEditPadPresses == 1) {
		// Find that original press
		for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
			if (instrumentClipView.editPadPresses[i].isActive) {
				// if found, calculate middle velocity between two velocity pad presses
				if (instrumentClipView.editPadPresses[i].xDisplay == x) {
					// the last pad press will have updated the default velocity
					// so get it as it will be used to calculate average between previous and new velocity
					int32_t previousVelocity = getCurrentInstrument()->defaultVelocity;

					// calculate middle velocity (average of two pad presses in a column)
					newVelocity = (newVelocity + previousVelocity) / 2;

					// update middle pad press selection indicator
					getMiddlePadPressSelected() = true;

					break;
				}
				// found a second press that isn't in the same column as the first press
				else {
					int32_t firstPadX = instrumentClipView.editPadPresses[i].xDisplay;

					// get note info on all the squares in the note row
					noteRow->getRowSquareInfo(effectiveLength, rowSquareInfo);
					setupVelocityRamp(firstPadX, rowSquareInfo[firstPadX].maxVelocity, x, y);

					// update multi pad press selection indicator
					getMultiPadPressSelected() = true;
					getMultiPadPressActive() = true;

					break;
				}
			}
		}
	}

	// if middle pad press was selected, set the velocity to middle velocity between two pads pressed
	if (getMiddlePadPressSelected()) {
		setVelocity(modelStackWithNoteRow, noteRow, x, newVelocity);
		refreshVelocityEditor = true;
	}
	// if multi pad (long) press was selected, set the velocity of all the notes between the two pad presses
	else if (getMultiPadPressSelected()) {
		setVelocityRamp(modelStackWithNoteRow, noteRow, rowSquareInfo);
		refreshVelocityEditor = true;
	}
	// otherwise, it's a regular velocity pad action
	else {
		// no existing notes in square pressed
		// add note and set velocity
		if (squareInfo.numNotes == 0) {
			addNoteWithNewVelocity(x, velocity, newVelocity);
			refreshVelocityEditor = true;
		}
		// pressing pad corresponding to the square's current (highest) velocity, remove note
		else if (minPadDisplayValues[y] <= squareInfo.maxVelocity && squareInfo.maxVelocity <= maxPadDisplayValues[y]) {
			recordNoteEditPadAction(x, velocity);
			refreshVelocityEditor = true;
			showNewVelocity = false;
		}
		// note(s) exists, adjust velocity of existing notes
		else {
			adjustNoteVelocity(modelStackWithNoteRow, noteRow, x, velocity, newVelocity, squareInfo.squareType);
			refreshVelocityEditor = true;
		}
	}
	// if no note exists and you're trying to remove a note (y == 0 && squareInfo.numNotes == 0),
	// well no need to do anything

	if (getMultiPadPressActive() && !isUIModeActive(UI_MODE_NOTES_PRESSED)) {
		getMultiPadPressActive() = false;
	}

	if (refreshVelocityEditor) {
		// refresh grid and update default velocity on the display
		uiNeedsRendering(getAutomationView(), 0xFFFFFFFF, 0);
		// if holding a multi pad press, render left and right velocity of the multi pad press
		if (getMultiPadPressActive()) {
			renderRampDisplay(x);
		}
		else {
			if (velocity) {
				renderDisplay(showNewVelocity ? newVelocity : squareInfo.maxVelocity);
			}
			else {
				renderDisplay();
			}
		}
	}
}

// convert y of pad press into velocity value between 1 and 127
int32_t AutomationEditorLayoutNoteVelocity::getVelocityFromY(int32_t y) {
	int32_t velocity = std::clamp<int32_t>(padPressValues[y], 1, 127);
	return velocity;
}

// convert velocity of a square into y
int32_t AutomationEditorLayoutNoteVelocity::getYFromVelocity(int32_t velocity) {
	for (int32_t i = 0; i < kDisplayHeight; i++) {
		if (minPadDisplayValues[i] <= velocity && velocity <= maxPadDisplayValues[i]) {
			return i;
		}
	}
	return kNoSelection;
}

// add note and set velocity
void AutomationEditorLayoutNoteVelocity::addNoteWithNewVelocity(int32_t x, int32_t velocity, int32_t newVelocity) {
	if (velocity) {
		// we change the instrument default velocity because it is used for new notes
		getCurrentInstrument()->defaultVelocity = newVelocity;
	}

	// record pad press and release
	// adds note with new velocity set
	recordNoteEditPadAction(x, velocity);
}

// adjust velocity of existing notes
void AutomationEditorLayoutNoteVelocity::adjustNoteVelocity(ModelStackWithNoteRow* modelStackWithNoteRow,
                                                            NoteRow* noteRow, int32_t x, int32_t velocity,
                                                            int32_t newVelocity, uint8_t squareType) {
	if (velocity) {
		// record pad press
		recordNoteEditPadAction(x, velocity);

		// adjust velocities of notes within pressed pad square
		setVelocity(modelStackWithNoteRow, noteRow, x, newVelocity);
	}
	else {
		// record pad release
		recordNoteEditPadAction(x, velocity);
	}
}

// set velocity of notes within pressed pad square
void AutomationEditorLayoutNoteVelocity::setVelocity(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow,
                                                     int32_t x, int32_t newVelocity) {
	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
	if (!action) {
		return;
	}

	int32_t velocityValue = 0;

	for (int32_t i = 0; i < kEditPadPressBufferSize; i++) {
		bool foundPadPress = instrumentClipView.editPadPresses[i].isActive;

		// if we found an active pad press and we're looking for a pad press with a specific xDisplay
		// see if the active pad press is the one we are looking for
		if (foundPadPress && (x != kNoSelection)) {
			foundPadPress = (instrumentClipView.editPadPresses[i].xDisplay == x);
		}

		if (foundPadPress) {
			instrumentClipView.editPadPresses[i].deleteOnDepress = false;

			// Multiple notes in square
			if (instrumentClipView.editPadPresses[i].isBlurredSquare) {

				uint32_t velocitySumThisSquare = 0;
				uint32_t numNotesThisSquare = 0;

				int32_t intendedPos = instrumentClipView.editPadPresses[i].intendedPos;
				int32_t intendedEndPos = intendedPos + instrumentClipView.editPadPresses[i].intendedLength;
				setRelativeVelocity(noteRow, modelStackWithNoteRow, action, intendedPos, intendedEndPos, newVelocity,
				                    [&](Note* note) {
					                    instrumentClipView.updateVelocityValue(velocityValue, note->getVelocity());
					                    numNotesThisSquare++;
					                    velocitySumThisSquare += note->getVelocity();
				                    });

				// Rohan: Get the average. Ideally we'd have done this when first selecting the note too, but I
				// didn't

				// Sean: not sure how getting the average when first selecting the note would help because the
				// average will change based on the velocity adjustment happening here.

				// We're adjusting the intendedVelocity here because this is the velocity that is used to audition
				// the pad press note so you can hear the velocity changes as you're holding the note down
				instrumentClipView.editPadPresses[i].intendedVelocity = velocitySumThisSquare / numNotesThisSquare;
			}

			// Only one note in square
			else {
				// We're adjusting the intendedVelocity here because this is the velocity that is used to audition
				// the pad press note so you can hear the velocity changes as you're holding the note down
				instrumentClipView.editPadPresses[i].intendedVelocity = newVelocity;
				noteRow->changeNotesAcrossAllScreens(instrumentClipView.editPadPresses[i].intendedPos,
				                                     modelStackWithNoteRow, action, CORRESPONDING_NOTES_SET_VELOCITY,
				                                     newVelocity);

				instrumentClipView.updateVelocityValue(velocityValue,
				                                       instrumentClipView.editPadPresses[i].intendedVelocity);
			}
		}
	}

	instrumentClipView.displayVelocity(velocityValue, 0);

	instrumentClipView.reassessAllAuditionStatus();
}

// set velocity of notes between pressed squares
void AutomationEditorLayoutNoteVelocity::setVelocityRamp(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow,
                                                         SquareInfo rowSquareInfo[kDisplayWidth]) {
	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
	if (!action) {
		return;
	}

	forEachRampStep(
	    getLeftPadSelectedX(), getRightPadSelectedX(), rampLeftVelocity, rampRightVelocity,
	    [&](int32_t x) { return rowSquareInfo[x].numNotes != 0 && rowSquareInfo[x].squareType != SQUARE_NOTE_TAIL; },
	    [&](int32_t x, int32_t velocity) {
		    setRelativeVelocity(noteRow, modelStackWithNoteRow, action, rowSquareInfo[x].squareStartPos,
		                        rowSquareInfo[x].squareEndPos, velocity, [](Note*) {});
	    });
}

/// set up a ramp between an existing press at firstX and a new press at (x, y). The first press keeps its current
/// velocity (firstVelocity) and the new press sets the other end. The ramp is stored as if it was entered left to
/// right.
void AutomationEditorLayoutNoteVelocity::setupVelocityRamp(int32_t firstX, int32_t firstVelocity, int32_t x,
                                                           int32_t y) {
	int32_t newVelocity = getVelocityFromY(y);
	bool firstIsLeft = firstX < x;
	getLeftPadSelectedX() = firstIsLeft ? firstX : x;
	getRightPadSelectedX() = firstIsLeft ? x : firstX;
	rampLeftVelocity = firstIsLeft ? firstVelocity : newVelocity;
	rampRightVelocity = firstIsLeft ? newVelocity : firstVelocity;
	getLeftPadSelectedY() = getYFromVelocity(rampLeftVelocity);
	getRightPadSelectedY() = getYFromVelocity(rampRightVelocity);
}

/// show the velocity at both ends of the ramp, or on 7seg the end at the pad pressed last
void AutomationEditorLayoutNoteVelocity::renderRampDisplay(int32_t x) {
	if (display->haveOLED()) {
		renderDisplay(rampLeftVelocity, rampRightVelocity);
	}
	else {
		renderDisplay(getLeftPadSelectedX() == x ? rampLeftVelocity : rampRightVelocity);
	}
}

// ---- folded velocity editor ----
// In the folded velocity editor every note row in the clip is shown in one velocity lane, so a pad press edits all
// notes that start on a step regardless of which row they are in.

namespace {
/// fn(modelStackWithNoteRow, noteRow, noteRowIndex) for every note row in the clip that has notes
template <typename F>
void forEachNoteRow(InstrumentClip* clip, ModelStackWithTimelineCounter* modelStack, F&& fn) {
	for (int32_t i = 0; i < clip->noteRows.getNumElements(); i++) {
		NoteRow* noteRow = clip->noteRows.getElement(i);
		if (!noteRow->notes.getNumElements()) {
			continue;
		}
		ModelStackWithNoteRow* modelStackWithNoteRow = modelStack->addNoteRow(clip->getNoteRowId(noteRow, i), noteRow);
		fn(modelStackWithNoteRow, noteRow, i);
	}
}

/// fn(modelStackWithNoteRow, noteRow, note) for every note starting in [startPos, endPos) in every note row
template <typename F>
void forEachFoldedNote(InstrumentClip* clip, ModelStackWithTimelineCounter* modelStack, int32_t startPos,
                       int32_t endPos, F&& fn) {
	forEachNoteRow(clip, modelStack, [&](ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, int32_t) {
		if (startPos < modelStackWithNoteRow->getLoopLength()) {
			forNotesInRange(noteRow, startPos, endPos, [&](Note* note) { fn(modelStackWithNoteRow, noteRow, note); });
		}
	});
}

/// the highest velocity of the notes starting on each step in any note row, or 0 if no note starts there.
/// Only steps with notes starting on them can be edited.
void getFoldedStepVelocities(InstrumentClip* clip, ModelStackWithTimelineCounter* modelStack,
                             int32_t stepVelocity[kDisplayWidth]) {
	std::fill(stepVelocity, stepVelocity + kDisplayWidth, 0);
	SquareInfo noteRowSquareInfo[kDisplayWidth];
	forEachNoteRow(clip, modelStack, [&](ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, int32_t) {
		noteRow->getRowSquareInfo(modelStackWithNoteRow->getLoopLength(), noteRowSquareInfo);
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			uint8_t type = noteRowSquareInfo[x].squareType;
			if (type == SQUARE_NOTE_HEAD || type == SQUARE_BLURRED) {
				stepVelocity[x] = std::max(stepVelocity[x], noteRowSquareInfo[x].maxVelocity);
			}
		}
	});
}

int32_t getStepStartPos(int32_t x) {
	return instrumentClipView.getPosFromSquare(x);
}
} // namespace

/// render the folded velocity editor, where every note row is drawn in its own colour (the same colour it has in the
/// clip view). Each row draws a bar up to its velocity on that step; where bars overlap, the shorter bar wins, so each
/// row shows as a band between its velocity and the next lower row's. Rows with notes starting on a step hide rows
/// that only have a tail there.
void AutomationEditorLayoutNoteVelocity::renderFoldedNoteEditor(
    InstrumentClip* clip, ModelStackWithTimelineCounter* modelStack, RGB image[][kDisplayWidth + kSideBarWidth],
    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	constexpr int16_t kNoVelocity = 1000;
	int16_t bestVelocity[kDisplayWidth][kDisplayHeight];
	RGB bestColour[kDisplayWidth][kDisplayHeight];
	bool anyHead[kDisplayWidth] = {};
	for (auto& column : bestVelocity) {
		std::fill(std::begin(column), std::end(column), kNoVelocity);
	}

	bool isKit = clip->output->type == OutputType::KIT;
	SquareInfo noteRowSquareInfo[kDisplayWidth];

	forEachNoteRow(clip, modelStack, [&](ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, int32_t i) {
		noteRow->getRowSquareInfo(modelStackWithNoteRow->getLoopLength(), noteRowSquareInfo);

		// same colours as the clip view (see InstrumentClip::renderAsSingleRow)
		RGB mainColour = clip->getMainColourFromY(isKit ? i : noteRow->y, noteRow->getColourOffset(clip));
		RGB tailColour = mainColour.forTail();
		RGB blurColour = mainColour.forBlur();

		for (int32_t x = 0; x < kDisplayWidth; x++) {
			SquareInfo& square = noteRowSquareInfo[x];
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
			for (int32_t y = 0; y < kDisplayHeight && square.maxVelocity >= minPadDisplayValues[y]; y++) {
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

/// scale every note starting in [startPos, endPos) in every note row so the highest one (maxVelocity) becomes
/// targetVelocity
void AutomationEditorLayoutNoteVelocity::setFoldedStepVelocity(ModelStackWithTimelineCounter* modelStack,
                                                               InstrumentClip* clip, Action* action, int32_t startPos,
                                                               int32_t endPos, int32_t maxVelocity,
                                                               int32_t targetVelocity) {
	float scale = getRelativeVelocityScale(maxVelocity, targetVelocity);
	forEachFoldedNote(clip, modelStack, startPos, endPos,
	                  [&](ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, Note* note) {
		                  noteRow->changeNotesAcrossAllScreens(note->pos, modelStackWithNoteRow, action,
		                                                       CORRESPONDING_NOTES_SET_VELOCITY,
		                                                       scaleVelocity(note->getVelocity(), scale));
	                  });
}

/// add offset to every note starting in [startPos, endPos) in every note row.
/// returns the highest velocity of the changed notes, or 0 if there were none
int32_t AutomationEditorLayoutNoteVelocity::adjustFoldedStepVelocity(ModelStackWithTimelineCounter* modelStack,
                                                                     InstrumentClip* clip, Action* action,
                                                                     int32_t startPos, int32_t endPos, int32_t offset) {
	int32_t maxVelocity = 0;
	forEachFoldedNote(clip, modelStack, startPos, endPos,
	                  [&](ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, Note* note) {
		                  noteRow->changeNotesAcrossAllScreens(note->pos, modelStackWithNoteRow, action,
		                                                       CORRESPONDING_NOTES_ADJUST_VELOCITY, offset);
		                  maxVelocity = std::max<int32_t>(maxVelocity, note->getVelocity());
	                  });
	return maxVelocity;
}

void AutomationEditorLayoutNoteVelocity::endFoldedPadPresses() {
	for (auto& press : foldedPadPresses) {
		press.isActive = false;
	}
	if (numFoldedPadPresses) {
		numFoldedPadPresses = 0;
		actionLogger.closeAction(ActionType::NOTE_EDIT);
	}
	getMultiPadPressActive() = false;
}

// folded velocity edit pad action
// 1) single press: set the velocity of every note starting on that step
// 2) two presses in one column: set the velocity to the middle of the two pads
// 3) two presses in different columns: ramp the velocity of every step between them
// empty steps are left alone because there is no single row to add a note to
void AutomationEditorLayoutNoteVelocity::foldedVelocityEditPadAction(ModelStackWithTimelineCounter* modelStack,
                                                                     InstrumentClip* clip, int32_t x, int32_t y,
                                                                     int32_t velocity) {
	// release
	if (!velocity) {
		for (auto& press : foldedPadPresses) {
			if (press.isActive && press.xDisplay == x && press.yDisplay == y) {
				if (numFoldedPadPresses == 1) {
					endFoldedPadPresses();
					renderDisplay();
				}
				else {
					press.isActive = false;
					numFoldedPadPresses--;
				}
				break;
			}
		}
		return;
	}

	int32_t stepVelocity[kDisplayWidth];
	getFoldedStepVelocities(clip, modelStack, stepVelocity);

	// only steps with notes starting on them can be edited
	if (!stepVelocity[x]) {
		return;
	}

	// find a previous press to combine with before registering this one
	FoldedPadPress* firstPress = nullptr;
	if (numFoldedPadPresses == 1) {
		for (auto& press : foldedPadPresses) {
			if (press.isActive) {
				firstPress = &press;
				break;
			}
		}
	}

	FoldedPadPress* newPress = nullptr;
	for (auto& press : foldedPadPresses) {
		if (!press.isActive) {
			newPress = &press;
			break;
		}
	}
	if (!newPress) {
		return;
	}
	*newPress = {true, x, y, getStepStartPos(x), getStepStartPos(x + 1)};
	numFoldedPadPresses++;

	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
	if (!action) {
		return;
	}

	getMiddlePadPressSelected() = false;
	getMultiPadPressSelected() = false;
	getLeftPadSelectedX() = x;

	int32_t newVelocity = getVelocityFromY(y);

	// two presses in the same column: middle velocity
	if (firstPress && firstPress->xDisplay == x) {
		newVelocity = (newVelocity + getVelocityFromY(firstPress->yDisplay)) / 2;
		getMiddlePadPressSelected() = true;
	}
	// two presses in different columns: ramp
	else if (firstPress) {
		setupVelocityRamp(firstPress->xDisplay, stepVelocity[firstPress->xDisplay], x, y);
		forEachRampStep(
		    getLeftPadSelectedX(), getRightPadSelectedX(), rampLeftVelocity, rampRightVelocity,
		    [&](int32_t i) { return stepVelocity[i] > 0; },
		    [&](int32_t i, int32_t rampVelocity) {
			    setFoldedStepVelocity(modelStack, clip, action, getStepStartPos(i), getStepStartPos(i + 1),
			                          stepVelocity[i], rampVelocity);
		    });

		getMultiPadPressSelected() = true;
		getMultiPadPressActive() = true;
		uiNeedsRendering(getAutomationView(), 0xFFFFFFFF, 0);
		renderRampDisplay(x);
		return;
	}

	setFoldedStepVelocity(modelStack, clip, action, getStepStartPos(x), getStepStartPos(x + 1), stepVelocity[x],
	                      newVelocity);
	uiNeedsRendering(getAutomationView(), 0xFFFFFFFF, 0);
	renderDisplay(newVelocity);
}

/// fine tune the velocity of every note on the held steps with the horizontal encoder
bool AutomationEditorLayoutNoteVelocity::foldedAdjustHeldVelocity(ModelStackWithTimelineCounter* modelStack,
                                                                  InstrumentClip* clip, int32_t offset) {
	if (!numFoldedPadPresses) {
		return false;
	}
	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
	if (!action) {
		return true;
	}
	int32_t displayVelocity = 0;
	for (auto& press : foldedPadPresses) {
		if (press.isActive) {
			int32_t highest = adjustFoldedStepVelocity(modelStack, clip, action, press.startPos, press.endPos, offset);
			if (highest) {
				displayVelocity = highest;
			}
		}
	}
	getMultiPadPressActive() = false;
	uiNeedsRendering(getAutomationView(), 0xFFFFFFFF, 0);
	if (displayVelocity) {
		renderDisplay(displayVelocity);
	}
	return true;
}

// }; // namespace deluge::gui::views::automation::editor_layout::note
