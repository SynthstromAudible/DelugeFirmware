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

#include "gui/views/automation/editor_layout.h"

// namespace deluge::gui::views::automation::editor_layout {

class AutomationEditorLayoutNote : public AutomationEditorLayout {
public:
	AutomationEditorLayoutNote() = default;

	// edit pad action
	void noteEditPadAction(ModelStackWithNoteRow* modelStackWithNoteRow, NoteRow* noteRow, InstrumentClip* clip,
	                       int32_t x, int32_t y, int32_t velocity, int32_t effectiveLength, SquareInfo& squareInfo);
	void recordNoteEditPadAction(int32_t x, int32_t velocity);

public:
	// Automation View Render Functions
	void renderNoteEditor(ModelStackWithNoteRow* modelStackWithNoteRow, InstrumentClip* clip,
	                      RGB image[][kDisplayWidth + kSideBarWidth],
	                      uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], int32_t renderWidth, int32_t xScroll,
	                      uint32_t xZoom, int32_t effectiveLength, int32_t xDisplay, bool drawUndefinedArea,
	                      SquareInfo& squareInfo);

public:
	void renderNoteEditorDisplayOLED(deluge::hid::display::oled_canvas::Canvas& canvas, InstrumentClip* clip,
	                                 OutputType outputType, int32_t knobPosLeft, int32_t knobPosRight);
	void renderNoteEditorDisplay7SEG(InstrumentClip* clip, OutputType outputType, int32_t knobPosLeft);
};

// }; // namespace deluge::gui::views::automation::editor_layout

// extern deluge::gui::views::automation::editor_layout::AutomationEditorLayoutNote automationEditorLayoutNote;

extern AutomationEditorLayoutNote automationEditorLayoutNote;
