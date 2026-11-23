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

#pragma once

#include "gui/views/automation/editor_layout/note.h"

// namespace deluge::gui::views::automation::editor_layout::note {

class AutomationEditorLayoutNoteVelocity : public AutomationEditorLayoutNote {
public:
	AutomationEditorLayoutNoteVelocity() = default;

	// edit pad action
	void velocityEditPadAction(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, InstrumentClip* clip,
	                           int32_t x, int32_t y, int32_t velocity, int32_t effectiveLength, SquareInfo& squareInfo);

	// folded velocity editor: every note row in the clip is shown and edited together
	void foldedVelocityEditPadAction(ModelStackWithTimelineCounter* modelStack, InstrumentClip* clip, int32_t x,
	                                 int32_t y, int32_t velocity);
	bool foldedAdjustHeldVelocity(ModelStackWithTimelineCounter* modelStack, InstrumentClip* clip, int32_t offset);
	void endFoldedPadPresses();
	bool anyFoldedPadPresses() const { return numFoldedPadPresses > 0; }
	static void renderFoldedNoteEditor(InstrumentClip* clip, ModelStackWithTimelineCounter* modelStack,
	                                   RGB image[][kDisplayWidth + kSideBarWidth],
	                                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);

private:
	int32_t getVelocityFromY(int32_t y);
	int32_t getYFromVelocity(int32_t velocity);
	void addNoteWithNewVelocity(int32_t x, int32_t velocity, int32_t newVelocity);
	void adjustNoteVelocity(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, int32_t x, int32_t velocity,
	                        int32_t newVelocity, uint8_t squareType);
	void setVelocity(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, int32_t x, int32_t newVelocity);
	void setVelocityRamp(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow,
	                     SquareInfo rowSquareInfo[kDisplayWidth]);

	// ramp between two presses in different columns, shared by the folded and unfolded editors
	void setupVelocityRamp(int32_t firstX, int32_t firstVelocity, int32_t x, int32_t y);
	void renderRampDisplay(int32_t x);
	int32_t rampLeftVelocity{0};
	int32_t rampRightVelocity{0};

	struct FoldedPadPress {
		bool isActive;
		int32_t xDisplay;
		int32_t yDisplay;
		int32_t startPos;
		int32_t endPos;
	};
	FoldedPadPress foldedPadPresses[kEditPadPressBufferSize]{};
	int32_t numFoldedPadPresses{0};

	void setFoldedStepVelocity(ModelStackWithTimelineCounter* modelStack, InstrumentClip* clip, Action* action,
	                           int32_t startPos, int32_t endPos, int32_t maxVelocity, int32_t targetVelocity);
	int32_t adjustFoldedStepVelocity(ModelStackWithTimelineCounter* modelStack, InstrumentClip* clip, Action* action,
	                                 int32_t startPos, int32_t endPos, int32_t offset);

public:
	// Automation View Render Functions
	void renderNoteColumn(ModelStackWithNoteRow* modelStackWithNoteRow, InstrumentClip* clip,
	                      RGB image[][kDisplayWidth + kSideBarWidth],
	                      uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xDisplay, int32_t xScroll,
	                      int32_t xZoom, SquareInfo& squareInfo);

private:
	void renderNoteSquare(RGB image[][kDisplayWidth + kSideBarWidth],
	                      uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t xDisplay, int32_t yDisplay,
	                      uint8_t squareType, int32_t value);

public:
	void displayParameterValueOLED(deluge::hid::display::oled_canvas::Canvas& canvas, int32_t yPos, int32_t knobPosLeft,
	                               int32_t knobPosRight);
};

// }; // namespace deluge::gui::views::automation::editor_layout::note

// extern deluge::gui::views::automation::editor_layout::AutomationEditorLayoutNoteVelocity
// automationEditorLayoutNoteVelocity;

extern AutomationEditorLayoutNoteVelocity automationEditorLayoutNoteVelocity;
