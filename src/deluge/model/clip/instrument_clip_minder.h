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
#include "storage/preset_acquisition.h"

#include <cstdint>

class InstrumentClip;
class Output;
class ModelStack;

// This class performs operations on an InstrumentClip that are common to both the InstrumentClipView,
// AutomationInstrumentClipView and KeyboardView.

class InstrumentClipMinder : public ClipMinder {
public:
	InstrumentClipMinder();
	virtual void displayOrLanguageChanged();
	/// @brief The card-reading half of new-instrument creation, unchanged and blocking. Legal only on
	///        the storage owner -- see beginNewInstrumentAcquisition()'s doc for why every caller now
	///        goes through that instead of this directly.
	/// @param newOutputType SYNTH or KIT (never MIDI/CV -- those never reach this function at all).
	/// @param is_dx Whether the new Synth should be FM (DX7). Ignored for KIT.
	/// @return Whether an instrument was actually created.
	bool createNewInstrument(OutputType newOutputType, bool is_dx = false);
	/// @brief Dispatch new-instrument creation onto the storage owner. Non-virtual, matching
	///        changeOutputType()'s shape and for the same reason: an unqualified call from a derived
	///        class's own scope resolves to THIS version, which is exactly why `consumer` defaults to
	///        the base tail (`MinderNewInstrument`) rather than InstrumentClipView's fuller one;
	///        InstrumentClipView::createNewInstrument() passes `ViewNewInstrument` explicitly instead.
	/// @param newOutputType SYNTH or KIT.
	/// @param is_dx Whether the new Synth should be FM (DX7). Ignored for KIT.
	/// @param consumer Which completion runs once createNewInstrument() actually runs; see
	///        `PresetConsumer`.
	/// @return Whether the gesture was accepted (dispatched, not yet complete) -- false only for the
	///         clip-not-empty guard, which runs before any card access.
	bool beginNewInstrumentAcquisition(
	    OutputType newOutputType, bool is_dx = false,
	    deluge::storage::PresetConsumer consumer = deluge::storage::PresetConsumer::MinderNewInstrument);
	/// @brief The MinderNewInstrument completion: runs createNewInstrument() on the storage owner with
	///        no further tail, matching what the direct SYNTH-button call sites this replaces did with
	///        its bool (discarded it).
	/// @param newOutputType SYNTH or KIT.
	/// @param is_dx Whether the new Synth should be FM (DX7). Ignored for KIT.
	void completeNewInstrument(OutputType newOutputType, bool is_dx);
	void setLedStates();
	virtual void focusRegained();
	virtual ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);
	void calculateDefaultRootNote();
	void drawActualNoteCode(int16_t noteCode);
	void cycleThroughScales();
	bool setScale(Scale newScale);
	void displayScaleName(Scale scale);
	void displayCurrentScaleName();
	void selectEncoderAction(int32_t offset);
	static void drawMIDIControlNumber(int32_t controlNumber, bool automationExists);
	static bool makeCurrentClipActiveOnInstrumentIfPossible(ModelStack* modelStack);
	/// @brief Change the current clip's output type. Non-virtual: an unqualified call from a derived
	///        class's own scope (e.g. KeyboardScreen's MIDI/CV buttons, which do not override this)
	///        resolves to THIS version, not a derived override -- which is exactly why `consumer`
	///        defaults to the base tail (`ViewChangeTypeMinder`) rather than InstrumentClipView's fuller
	///        one; InstrumentClipView::changeOutputType() passes `ViewChangeType` explicitly instead.
	/// @param newOutputType The type to switch to.
	/// @param consumer Which completion runs once the swap (if any) actually happens; see
	///        `PresetConsumer`.
	/// @return Whether the gesture was accepted -- for Synth/Kit this means dispatched, not yet
	///         complete; see View::changeOutputType()'s doc.
	bool
	changeOutputType(OutputType newOutputType,
	                 deluge::storage::PresetConsumer consumer = deluge::storage::PresetConsumer::ViewChangeTypeMinder);

	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas);

	static int16_t defaultRootNote; // Stores the calculated "default" root note between the user pressing the
	                                // scale-mode button and releasing it
	static bool toggleScaleModeOnButtonRelease;
	static uint32_t scaleButtonPressTime;
	static bool flashDefaultRootNoteOn;

	static uint8_t editingMIDICCForWhichModKnob;
};
