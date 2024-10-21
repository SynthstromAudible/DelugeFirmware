/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include "hid/button.h"
#include "hid/display/oled_canvas/canvas.h"
#include "model/clip/clip_minder.h"
#include "model/scale/preset_scales.h"

#include <cstdint>

class InstrumentClip;
class Output;
class ModelStack;

// This class performs operations on an InstrumentClip that are common to both the InstrumentClipView,
// AutomationInstrumentClipView and KeyboardView.

class InstrumentClipMinder : public ClipMinder {
public:
	InstrumentClipMinder();
	static void redrawNumericDisplay();
	void displayOrLanguageChanged();
	bool createNewInstrument(OutputType newOutputType, bool is_dx = false);
	void setLedStates();
	void focusRegained();
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);
	void calculateDefaultRootNote();
	void drawActualNoteCode(int16_t noteCode);
	void cycleThroughScales();
	bool setScale(Scale newScale);
	void displayScaleName(Scale scale);
	void displayCurrentScaleName();
	void selectEncoderAction(int32_t offset);
	static void drawMIDIControlNumber(int32_t controlNumber, bool automationExists);
	static bool makeCurrentClipActiveOnInstrumentIfPossible(ModelStack* modelStack);
	bool changeOutputType(OutputType newOutputType);

	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas);

	static int16_t defaultRootNote; // Stores the calculated "default" root note between the user pressing the
	                                // scale-mode button and releasing it
	static bool toggleScaleModeOnButtonRelease;
	static uint32_t scaleButtonPressTime;
	static bool flashDefaultRootNoteOn;

	static uint8_t editingMIDICCForWhichModKnob;
};
