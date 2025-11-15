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
#include "model/note/note.h"

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

/// render each square in each column of the note editor grid
PLACE_SDRAM_TEXT void AutomationEditorLayoutNoteVelocity::renderNoteColumn(
    ModelStackWithNoteRow* modelStackWithNoteRow, InstrumentClip* clip, RGB image[][kDisplayWidth + kSideBarWidth],
    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xDisplay, int32_t xScroll, int32_t xZoom,
    SquareInfo& squareInfo) {
	// iterate through each square
	for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
		renderNoteSquare(image, occupancyMask, xDisplay, yDisplay, squareInfo.squareType, squareInfo.averageVelocity);
	}
}

/// render column for note parameter
PLACE_SDRAM_TEXT void AutomationEditorLayoutNoteVelocity::renderNoteSquare(
    RGB image[][kDisplayWidth + kSideBarWidth], uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
    int32_t xDisplay, int32_t yDisplay, uint8_t squareType, int32_t value) {
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
PLACE_SDRAM_TEXT void
AutomationEditorLayoutNoteVelocity::displayParameterValueOLED(deluge::hid::display::oled_canvas::Canvas& canvas,
                                                              int32_t yPos, int32_t knobPosLeft, int32_t knobPosRight) {
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
PLACE_SDRAM_TEXT void AutomationEditorLayoutNoteVelocity::velocityEditPadAction(
    ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, InstrumentClip* clip, int32_t x, int32_t y,
    int32_t velocity, int32_t effectiveLength, SquareInfo& squareInfo) {
	// save pad selected
	getLeftPadSelectedX() = x;

	// calculate new velocity based on Y of pad pressed
	int32_t newVelocity = getVelocityFromY(y);

	// middle pad press variables
	getMiddlePadPressSelected() = false;

	// multi pad press variables
	getMultiPadPressSelected() = false;
	SquareInfo rowSquareInfo[kDisplayWidth];
	int32_t multiPadPressVelocityIncrement = 0;

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

					// the long press logic calculates and renders the interpolation as if the press was
					// entered in a forward fashion (where the first pad is to the left of the second
					// pad). if the user happens to enter a long press backwards then we fix that entry
					// by re-ordering the pad presses so that it is forward again
					getLeftPadSelectedX() = firstPadX > x ? x : firstPadX;
					getRightPadSelectedX() = firstPadX > x ? firstPadX : x;

					int32_t numSquares = 0;
					// find total number of notes in note row (excluding the first note)
					for (int32_t i = getLeftPadSelectedX(); i <= getRightPadSelectedX(); i++) {
						// don't include note tails in note count
						if (rowSquareInfo[i].numNotes != 0 && rowSquareInfo[i].squareType != SQUARE_NOTE_TAIL) {
							numSquares++;
						}
					}

					//	DEF_STACK_STRING_BUF(numSquare, 50);
					//	numSquare.append("Squares: ");
					//	numSquare.appendInt(numSquares);
					//	numSquare.append("\n");

					// calculate start and end velocity for long press
					int32_t leftPadSelectedVelocity;
					int32_t rightPadSelectedVelocity;

					if (getLeftPadSelectedX() == firstPadX) { // then left pad is the first press
						leftPadSelectedVelocity = rowSquareInfo[getLeftPadSelectedX()].averageVelocity;
						getLeftPadSelectedY() = getYFromVelocity(leftPadSelectedVelocity);
						rightPadSelectedVelocity = getVelocityFromY(y);
						getRightPadSelectedY() = y;
					}
					else { // then left pad is the second press
						leftPadSelectedVelocity = getVelocityFromY(y);
						getLeftPadSelectedY() = y;
						rightPadSelectedVelocity = rowSquareInfo[getRightPadSelectedX()].averageVelocity;
						getRightPadSelectedY() = getYFromVelocity(rightPadSelectedVelocity);
					}

					//	numSquare.append("L: ");
					//	numSquare.appendInt(leftPadSelectedVelocity);
					//	numSquare.append(" R: ");
					//	numSquare.appendInt(rightPadSelectedVelocity);
					//	numSquare.append("\n");

					// calculate increment from first pad to last pad
					float multiPadPressVelocityIncrementFloat =
					    static_cast<float>((rightPadSelectedVelocity - leftPadSelectedVelocity)) / (numSquares - 1);
					multiPadPressVelocityIncrement =
					    static_cast<int32_t>(std::round(multiPadPressVelocityIncrementFloat));
					// if ramp is upwards, make increment positive
					if (leftPadSelectedVelocity < rightPadSelectedVelocity) {
						multiPadPressVelocityIncrement = std::abs(multiPadPressVelocityIncrement);
					}

					//	numSquare.append("Inc: ");
					//	numSquare.appendInt(multiPadPressVelocityIncrement);
					//	display->displayPopup(numSquare.c_str());

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
		setVelocityRamp(modelStackWithNoteRow, noteRow, rowSquareInfo, multiPadPressVelocityIncrement);
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
		// pressing pad corresponding to note's current averageVelocity, remove note
		else if (minPadDisplayValues[y] <= squareInfo.averageVelocity
		         && squareInfo.averageVelocity <= maxPadDisplayValues[y]) {
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
			int32_t leftPadSelectedVelocity = getVelocityFromY(getLeftPadSelectedY());
			int32_t rightPadSelectedVelocity = getVelocityFromY(getRightPadSelectedY());
			if (display->haveOLED()) {
				renderDisplay(leftPadSelectedVelocity, rightPadSelectedVelocity);
			}
			else {
				// for 7seg, render value of last pad pressed
				renderDisplay(getLeftPadSelectedX() == x ? leftPadSelectedVelocity : rightPadSelectedVelocity);
			}
		}
		else {
			if (velocity) {
				renderDisplay(showNewVelocity ? newVelocity : squareInfo.averageVelocity);
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
PLACE_SDRAM_TEXT void AutomationEditorLayoutNoteVelocity::addNoteWithNewVelocity(int32_t x, int32_t velocity,
                                                                                 int32_t newVelocity) {
	if (velocity) {
		// we change the instrument default velocity because it is used for new notes
		getCurrentInstrument()->defaultVelocity = newVelocity;
	}

	// record pad press and release
	// adds note with new velocity set
	recordNoteEditPadAction(x, velocity);
}

// adjust velocity of existing notes
PLACE_SDRAM_TEXT void
AutomationEditorLayoutNoteVelocity::adjustNoteVelocity(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow,
                                                       int32_t x, int32_t velocity, int32_t newVelocity,
                                                       uint8_t squareType) {
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
PLACE_SDRAM_TEXT void AutomationEditorLayoutNoteVelocity::setVelocity(ModelStackWithNoteRow* modelStackWithNoteRow,
                                                                      NoteRow* noteRow, int32_t x,
                                                                      int32_t newVelocity) {
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

				int32_t noteI =
				    noteRow->notes.search(instrumentClipView.editPadPresses[i].intendedPos, GREATER_OR_EQUAL);
				Note* note = noteRow->notes.getElement(noteI);
				while (note
				       && note->pos - instrumentClipView.editPadPresses[i].intendedPos
				              < instrumentClipView.editPadPresses[i].intendedLength) {
					noteRow->changeNotesAcrossAllScreens(note->pos, modelStackWithNoteRow, action,
					                                     CORRESPONDING_NOTES_SET_VELOCITY, newVelocity);

					instrumentClipView.updateVelocityValue(velocityValue, note->getVelocity());

					numNotesThisSquare++;
					velocitySumThisSquare += note->getVelocity();

					noteI++;
					note = noteRow->notes.getElement(noteI);
				}

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
PLACE_SDRAM_TEXT void AutomationEditorLayoutNoteVelocity::setVelocityRamp(ModelStackWithNoteRow* modelStackWithNoteRow,
                                                                          NoteRow* noteRow,
                                                                          SquareInfo rowSquareInfo[kDisplayWidth],
                                                                          int32_t velocityIncrement) {
	Action* action = actionLogger.getNewAction(ActionType::NOTE_EDIT, ActionAddition::ALLOWED);
	if (!action) {
		return;
	}

	int32_t startVelocity = getVelocityFromY(getLeftPadSelectedY());
	int32_t velocityValue = 0;
	int32_t squaresProcessed = 0;

	for (int32_t i = getLeftPadSelectedX(); i <= getRightPadSelectedX(); i++) {
		if (rowSquareInfo[i].numNotes != 0) {
			int32_t intendedPos = rowSquareInfo[i].squareStartPos;

			// Multiple notes in square
			if (rowSquareInfo[i].numNotes > 1) {
				int32_t intendedLength = rowSquareInfo[i].squareEndPos - intendedPos;

				int32_t noteI = noteRow->notes.search(intendedPos, GREATER_OR_EQUAL);

				Note* note = noteRow->notes.getElement(noteI);

				while (note && note->pos - intendedPos < intendedLength) {
					int32_t intendedVelocity =
					    std::clamp<int32_t>(startVelocity + (velocityIncrement * squaresProcessed), 1, 127);

					noteRow->changeNotesAcrossAllScreens(note->pos, modelStackWithNoteRow, action,
					                                     CORRESPONDING_NOTES_SET_VELOCITY, intendedVelocity);

					noteI++;

					note = noteRow->notes.getElement(noteI);
				}
			}
			// one note in square
			else {
				int32_t intendedVelocity =
				    std::clamp<int32_t>(startVelocity + (velocityIncrement * squaresProcessed), 1, 127);

				noteRow->changeNotesAcrossAllScreens(intendedPos, modelStackWithNoteRow, action,
				                                     CORRESPONDING_NOTES_SET_VELOCITY, intendedVelocity);
			}

			// don't include note tails in note count
			if (rowSquareInfo[i].squareType != SQUARE_NOTE_TAIL) {
				squaresProcessed++;
			}
		}
	}
}

// }; // namespace deluge::gui::views::automation::editor_layout::note
