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
#include "gui/colour/colour.h"
#include <cstdint>

class Clip;

// The note-view / clip-grid macro target picker: while a macro button is held in gold-knob MACRO mode,
// the active clip-minder grid view (note view, or an audio clip's waveform) hands its main pads to this
// overlay - a parameter picker built on Macros::macroDestinationForPad. Tapping a pad assigns that param
// to the held macro's next free target slot; re-tapping cycles a pad's shared second shortcut layer;
// SHIFT+SAVE removes the selected pad's assignment; and while a slot is selected the gold knobs shape its
// From/To. Its interaction model is multi-slot build-up / cycle-on-release.
//
// This is DISTINCT from the automation-lane held-target picker in AutomationView (hold a target
// quick-edit button on a macro lane -> pick a destination from the automation-overview grid), which has
// its own single-held-slot, commit-on-release model and is deliberately not unified with this one.
//
// It is view-agnostic - it resolves the *current* clip's domain each call - so both InstrumentClipView
// and AudioClipView drive the one global instance without depending on each other.
class MacroAssignOverlay {
public:
	bool active() const { return heldMacro_ >= 0; }
	// True once a target slot has been selected by a tap: the gold knobs then shape that target's
	// From/To (knob 0 = From, knob 1 = To) instead of driving the whole macro.
	bool editingTarget() const { return active() && lastSlot_ >= 0; }

	void open(int32_t macroIndex);
	void close();
	void pulse(); // uiTimerManager advances the both-layers-assigned blink while the picker is up

	void renderOverlay(RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);
	void handlePad(int32_t x, int32_t y, int32_t velocity);
	void handleModEncoder(int32_t whichModEncoder, int32_t offset);
	void deleteSelectedTarget(); // SHIFT+SAVE: remove the selected pad's assignment (pad reverts to grey)

private:
	// Assigns `destination` to the macro's next free target slot and selects it (or shows MACRO SLOTS
	// FULL). Used for both the grey-pad assign and the build-up add of a second shortcut layer.
	void addLayer(Clip* clip, int32_t x, int32_t y, int32_t destination, bool addingSecond);
	// Shows the selected target's range readout + knob rings on tap / selection change.
	void showReadout();

	int8_t heldMacro_ = -1; // held macro (0..kNumMacros-1), or -1 when the picker is inactive
	int8_t lastX_ = -1;
	int8_t lastY_ = -1;
	int8_t lastSlot_ = -1;
	bool secondLayer_ = false;
	// Re-tapping the already-selected both-layer pad defers the primary<->second cycle to pad RELEASE,
	// so press-and-hold keeps the shown layer (SHIFT+DELETE then hits what you see).
	bool cycleOnRelease_ = false;
	bool altPhase_ = false; // slow white/yellow blink phase for both-layers-assigned pads
};

extern MacroAssignOverlay macroAssignOverlay;
