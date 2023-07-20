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

#include "RZA1/system/r_typedefs.h"
#include "definitions_cxx.hpp"
#include "hid/button.h"
#include "model/clip/clip_minder.h"

class InstrumentClip;
class Output;
class ModelStack;

// This class performs operations on an InstrumentClip that are common to both the InstrumentClipView and KeyboardView.

class InstrumentClipMinder : public ClipMinder {
public:
	InstrumentClipMinder();
	static void redrawNumericDisplay();
	void createNewInstrument(InstrumentType newInstrumentType);
	void setLedStates();
	void focusRegained();
	int buttonAction(hid::Button b, bool on, bool inCardRoutine);
	void calculateDefaultRootNote();
	void drawActualNoteCode(int16_t noteCode);
	void cycleThroughScales();
	void displayScaleName(int scale);
	void displayCurrentScaleName();
	void selectEncoderAction(int offset);
	static void drawMIDIControlNumber(int controlNumber, bool automationExists);
	bool makeCurrentClipActiveOnInstrumentIfPossible(ModelStack* modelStack);
	void changeInstrumentType(InstrumentType newInstrumentType);
	void opened();

#if HAVE_OLED
	void renderOLED(uint8_t image[][OLED_MAIN_WIDTH_PIXELS]);
#endif

	static int16_t
	    defaultRootNote; // Stores the calculated "default" root note between the user pressing the scale-mode button and releasing it
	static bool exitScaleModeOnButtonRelease;
	static bool flashDefaultRootNoteOn;

	static uint8_t editingMIDICCForWhichModKnob;
};
