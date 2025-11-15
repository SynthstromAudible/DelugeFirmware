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

#include "gui/views/automation/editor_layout/note.h"
#include "gui/views/automation/editor_layout/note/velocity.h"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/instrument_clip.h"

// namespace deluge::gui::views::automation::editor_layout {

using namespace deluge::gui;

PLACE_SDRAM_BSS AutomationEditorLayoutNote automationEditorLayoutNote{};

// gets the length of the note row, renders the pads corresponding to current note parameter values set up to the
// note row length renders the undefined area of the note row that the user can't interact with
PLACE_SDRAM_TEXT void AutomationEditorLayoutNote::renderNoteEditor(
    ModelStackWithNoteRow* modelStackWithNoteRow, InstrumentClip* clip, RGB image[][kDisplayWidth + kSideBarWidth],
    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t renderWidth, int32_t xScroll, uint32_t xZoom,
    int32_t effectiveLength, int32_t xDisplay, bool drawUndefinedArea, SquareInfo& squareInfo) {
	if (modelStackWithNoteRow->getNoteRowAllowNull()) {
		if (getAutomationParamType() == AutomationParamType::NOTE_VELOCITY) {
			automationEditorLayoutNoteVelocity.renderNoteColumn(modelStackWithNoteRow, clip, image, occupancyMask,
			                                                    xDisplay, xScroll, xZoom, squareInfo);
		}
	}
	if (drawUndefinedArea) {
		renderUndefinedArea(xScroll, xZoom, effectiveLength, image, occupancyMask, renderWidth,
		                    getAutomationView()->toTimelineView(), currentSong->tripletsOn, xDisplay);
	}
}

PLACE_SDRAM_TEXT void
AutomationEditorLayoutNote::renderNoteEditorDisplayOLED(deluge::hid::display::oled_canvas::Canvas& canvas,
                                                        InstrumentClip* clip, OutputType outputType,
                                                        int32_t knobPosLeft, int32_t knobPosRight) {
	// display note parameter name
	DEF_STACK_STRING_BUF(parameterName, 30);
	if (getAutomationParamType() == AutomationParamType::NOTE_VELOCITY) {
		parameterName.append("Velocity");
	}

#if OLED_MAIN_HEIGHT_PIXELS == 64
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 12;
#else
	int32_t yPos = OLED_MAIN_TOPMOST_PIXEL + 3;
#endif
	canvas.drawStringCentredShrinkIfNecessary(parameterName.c_str(), yPos, kTextSpacingX, kTextSpacingY);

	// display note / drum name
	yPos = yPos + 12;

	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	bool isKit = outputType == OutputType::KIT;

	ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay,
	                                                                        modelStack); // don't create
	if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
		if (!isKit) {
			modelStackWithNoteRow =
			    instrumentClipView.createNoteRowForYDisplay(modelStack, instrumentClipView.lastAuditionedYDisplay);
		}
	}

	char noteRowName[50];

	if (modelStackWithNoteRow->getNoteRowAllowNull()) {
		if (isKit) {
			DEF_STACK_STRING_BUF(drumName, 50);
			instrumentClipView.getDrumName(modelStackWithNoteRow->getNoteRow()->drum, drumName);
			strncpy(noteRowName, drumName.c_str(), 49);
		}
		else {
			int32_t isNatural = 1; // gets modified inside noteCodeToString to be 0 if sharp.
			noteCodeToString(modelStackWithNoteRow->getNoteRow()->getNoteCode(), noteRowName, &isNatural);
		}
	}
	else {
		if (isKit) {
			strncpy(noteRowName, "(Select Drum)", 49);
		}
		else {
			strncpy(noteRowName, "(Select Note)", 49);
		}
	}

	canvas.drawStringCentred(noteRowName, yPos, kTextSpacingX, kTextSpacingY);

	// display parameter value
	yPos = yPos + 12;

	if (getAutomationParamType() == AutomationParamType::NOTE_VELOCITY) {
		automationEditorLayoutNoteVelocity.displayParameterValueOLED(canvas, yPos, knobPosLeft, knobPosRight);
	}
}

PLACE_SDRAM_TEXT void AutomationEditorLayoutNote::renderNoteEditorDisplay7SEG(InstrumentClip* clip,
                                                                              OutputType outputType,
                                                                              int32_t knobPosLeft) {
	char modelStackMemory[MODEL_STACK_MAX_SIZE];
	ModelStackWithTimelineCounter* modelStack = currentSong->setupModelStackWithCurrentClip(modelStackMemory);
	bool isKit = outputType == OutputType::KIT;

	ModelStackWithNoteRow* modelStackWithNoteRow = clip->getNoteRowOnScreen(instrumentClipView.lastAuditionedYDisplay,
	                                                                        modelStack); // don't create
	if (!modelStackWithNoteRow->getNoteRowAllowNull()) {
		if (!isKit) {
			modelStackWithNoteRow =
			    instrumentClipView.createNoteRowForYDisplay(modelStack, instrumentClipView.lastAuditionedYDisplay);
		}
	}

	if (knobPosLeft != kNoSelection) {
		char buffer[5];
		intToString(knobPosLeft, buffer);
		display->setText(buffer, true, 255, false);
	}
	else {
		// display note / drum name
		char noteRowName[50];
		if (modelStackWithNoteRow->getNoteRowAllowNull()) {
			if (isKit) {
				DEF_STACK_STRING_BUF(drumName, 50);
				instrumentClipView.getDrumName(modelStackWithNoteRow->getNoteRow()->drum, drumName);
				strncpy(noteRowName, drumName.c_str(), 49);
			}
			else {
				int32_t isNatural = 1; // gets modified inside noteCodeToString to be 0 if sharp.
				noteCodeToString(modelStackWithNoteRow->getNoteRow()->getNoteCode(), noteRowName, &isNatural);
			}
		}
		else {
			if (isKit) {
				strncpy(noteRowName, "(Select Drum)", 49);
			}
			else {
				strncpy(noteRowName, "(Select Note)", 49);
			}
		}
		display->setScrollingText(noteRowName);
	}
}

// note edit pad action
// handles single and multi pad presses for note parameter editing (e.g. velocity)
// stores pad presses in the EditPadPresses struct of the instrument clip view
PLACE_SDRAM_TEXT void AutomationEditorLayoutNote::noteEditPadAction(ModelStackWithNoteRow* modelStackWithNoteRow,
                                                                    NoteRow* noteRow, InstrumentClip* clip, int32_t x,
                                                                    int32_t y, int32_t velocity,
                                                                    int32_t effectiveLength, SquareInfo& squareInfo) {
	if (getAutomationParamType() == AutomationParamType::NOTE_VELOCITY) {
		automationEditorLayoutNoteVelocity.velocityEditPadAction(modelStackWithNoteRow, noteRow, clip, x, y, velocity,
		                                                         effectiveLength, squareInfo);
	}
}

// call instrument clip view edit pad action function to process pad press actions
PLACE_SDRAM_TEXT void AutomationEditorLayoutNote::recordNoteEditPadAction(int32_t x, int32_t velocity) {
	instrumentClipView.editPadAction(velocity, instrumentClipView.lastAuditionedYDisplay, x,
	                                 currentSong->xZoom[NAVIGATION_CLIP]);
}

// }; // namespace deluge::gui::views::automation::editor_layout
