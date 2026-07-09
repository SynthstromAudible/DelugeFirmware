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

#include "gui/views/macro_assign_overlay.h"
#include "gui/colour/colour.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "model/clip/clip.h"
#include "model/output.h"
#include "model/song/song.h"
#include "modulation/macros/macros.h"

using namespace deluge::gui;

MacroAssignOverlay macroAssignOverlay{};

void MacroAssignOverlay::open(int32_t macroIndex) {
	heldMacro_ = (int8_t)macroIndex;
	lastX_ = -1;
	lastY_ = -1;
	lastSlot_ = -1;
	secondLayer_ = false;
	cycleOnRelease_ = false;
	altPhase_ = false;
	// slow alternation for any pad with both shortcut layers assigned (~0.7 Hz)
	uiTimerManager.setTimer(TimerName::MACRO_ASSIGN_OVERLAY_PULSE, 700);
	// repaint whichever clip-minder grid is showing (note view, or an audio clip's waveform) so the
	// picker overlay takes over the main pads
	uiNeedsRendering(getRootUI(), 0xFFFFFFFF, 0);
}

void MacroAssignOverlay::close() {
	if (heldMacro_ < 0) {
		return;
	}
	heldMacro_ = -1;
	uiTimerManager.unsetTimer(TimerName::MACRO_ASSIGN_OVERLAY_PULSE);
	uiNeedsRendering(getRootUI(), 0xFFFFFFFF, 0); // back to the view's normal main pads (notes / waveform)
}

void MacroAssignOverlay::pulse() {
	if (!active()) {
		return; // picker closed - let the timer lapse
	}
	altPhase_ = !altPhase_;
	uiNeedsRendering(getRootUI(), 0xFFFFFFFF, 0);
	uiTimerManager.setTimer(TimerName::MACRO_ASSIGN_OVERLAY_PULSE, 700); // re-arm
}

// The picker overlay: assignable params light grey; params already targeted by the held macro (either
// shortcut layer) light bright; pads that aren't a valid destination go dark. Same resolution as the
// automation-view grid picker, reused via Macros::macroDestinationForPad.
void MacroAssignOverlay::renderOverlay(RGB image[][kDisplayWidth + kSideBarWidth],
                                       uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	Clip* clip = getCurrentClip();
	Output* instrument = Macros::macroHost(clip);
	if (instrument == nullptr || heldMacro_ < 0) {
		return;
	}
	Macros::Domain domain = Macros::domainForOutput(clip->output);
	Macros::Macro& macro = instrument->macros[heldMacro_];
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			int32_t primary = Macros::macroDestinationForPad(domain, x, y, false);
			int32_t second = Macros::macroDestinationForPad(domain, x, y, true);
			if (primary < 0 && second < 0) {
				image[y][x] = colours::black; // not a valid destination
				continue;
			}
			// Which of this pad's two shortcut layers are already assigned to this macro?
			Macros::LayerAssignment la = Macros::layerAssignment(macro, primary, second);
			bool primaryAssigned = la.primarySlot >= 0;
			bool secondAssigned = la.secondSlot >= 0;
			if (primaryAssigned && secondAssigned) {
				// Both layers assigned. The pad blinks (white<->yellow) until it's the one you're on: the
				// currently-held/last-tapped pad stops blinking and shows its selected layer solid, so you can
				// see which one a tap or SHIFT+SAVE will act on.
				if (x == lastX_ && y == lastY_) {
					image[y][x] = secondLayer_ ? colours::yellow : colours::white_full;
				}
				else {
					image[y][x] = altPhase_ ? colours::yellow : colours::white_full;
				}
			}
			else if (primaryAssigned) {
				image[y][x] = colours::white_full; // primary shortcut assigned
			}
			else if (secondAssigned) {
				image[y][x] = colours::yellow; // second-layer shortcut assigned (matches its yellow elsewhere)
			}
			else {
				image[y][x] = colours::grey; // assignable, not yet assigned
			}
			if (occupancyMask) {
				occupancyMask[y][x] = 64;
			}
		}
	}
}

// A pad tap in the picker: assign that param to the macro's next free target slot. When a pad's
// primary and second-layer params are both targets, the selection cycles between them - but that
// cycle happens on pad RELEASE, not press, so press-and-holding the pad keeps the shown layer stable
// (SHIFT+DELETE then removes exactly the layer you see; a plain tap still cycles on release).
void MacroAssignOverlay::handlePad(int32_t x, int32_t y, int32_t velocity) {
	Clip* clip = getCurrentClip();
	Output* instrument = Macros::macroHost(clip);
	if (instrument == nullptr || heldMacro_ < 0) {
		return;
	}
	Macros::Domain domain = Macros::domainForOutput(clip->output);
	int32_t primary = Macros::macroDestinationForPad(domain, x, y, false);
	int32_t second = Macros::macroDestinationForPad(domain, x, y, true);
	if (primary < 0 && second < 0) {
		return; // a dark pad - not a pickable destination
	}
	Macros::Macro& macro = instrument->macros[heldMacro_];

	// Is this pad's param already a target of the macro? If so we EDIT that existing slot rather than
	// adding a duplicate slot pointing at the same destination.
	Macros::LayerAssignment la = Macros::layerAssignment(macro, primary, second);
	int32_t primarySlot = la.primarySlot;
	int32_t secondSlot = la.secondSlot;
	bool bothAssigned = (primarySlot >= 0 && secondSlot >= 0);

	if (!velocity) {
		// RELEASE: perform the mutation deferred on press. Keeping it off the press means a press-and-hold
		// keeps the shown layer stable for SHIFT+DELETE (which clears the flag, cancelling this). The
		// assignment is rechecked, so a delete during the hold correctly changes the outcome.
		if (cycleOnRelease_ && x == lastX_ && y == lastY_) {
			if (bothAssigned) {
				// cycle the selection primary <-> second
				secondLayer_ = !secondLayer_;
				lastSlot_ = secondLayer_ ? secondSlot : primarySlot;
				uiNeedsRendering(getRootUI(), 0xFFFFFFFF, 0);
				showReadout();
			}
			else {
				// build-up: add the layer this pad doesn't yet hold (no-op for a single-layer pad)
				int32_t toAdd = -1;
				bool addingSecond = false;
				if (secondSlot >= 0 && primarySlot < 0 && primary >= 0) {
					toAdd = primary; // second-layer assigned, add its primary -> both
				}
				else if (primarySlot >= 0 && secondSlot < 0 && second >= 0) {
					toAdd = second; // primary assigned, add its second layer -> both
					addingSecond = true;
				}
				if (toAdd >= 0) {
					addLayer(clip, x, y, toAdd, addingSecond);
				}
			}
		}
		cycleOnRelease_ = false;
		return;
	}

	// PRESS: SELECT the pad and DEFER any mutation to release, so a press-and-hold keeps the shown layer
	// for SHIFT+DELETE. Exception: a grey (unassigned) pad has nothing to protect, so it assigns now.
	cycleOnRelease_ = false;

	if (bothAssigned) {
		// Re-tapping the pad we're on defers the primary<->second cycle to release; a DIFFERENT both-layer
		// pad just selects its primary (a further tap then cycles it).
		if (x == lastX_ && y == lastY_ && lastSlot_ >= 0) {
			cycleOnRelease_ = true;
			showReadout(); // refresh the readout for the hold; layer unchanged
			return;
		}
		secondLayer_ = false;
		lastSlot_ = primarySlot;
	}
	else if (primarySlot >= 0 || secondSlot >= 0) {
		// One layer assigned: SELECT its layer and defer the build-up (adding the OTHER layer) to release,
		// so a press-and-hold shows this layer for SHIFT+DELETE while a plain tap still builds up on release.
		secondLayer_ = (secondSlot >= 0);
		lastSlot_ = (primarySlot >= 0) ? primarySlot : secondSlot;
		cycleOnRelease_ = true;
		lastX_ = x;
		lastY_ = y;
		uiNeedsRendering(getRootUI(), 0xFFFFFFFF, 0); // a previously-selected both-layer pad resumes blinking
		showReadout();
		return;
	}
	else {
		// Grey pad: assign the first layer immediately (primary, or the second if the pad has no primary).
		addLayer(clip, x, y, (primary >= 0) ? primary : second, /*addingSecond=*/(primary < 0));
		return;
	}
	lastX_ = x;
	lastY_ = y;
	uiNeedsRendering(getRootUI(), 0xFFFFFFFF, 0); // refresh the assigned-highlight
	showReadout();
}

// Assigns `destination` to the macro's next free target slot and selects it, or shows MACRO SLOTS FULL.
void MacroAssignOverlay::addLayer(Clip* clip, int32_t x, int32_t y, int32_t destination, bool addingSecond) {
	Output* instrument = Macros::macroHost(clip);
	if (instrument == nullptr) {
		return;
	}
	Macros::Macro& macro = instrument->macros[heldMacro_];
	int32_t freeSlot = -1;
	for (int32_t s = 0; s < Macros::kNumTargetSlots; s++) {
		if (macro.targets[s].destination == Macros::kNoDestination) {
			freeSlot = s;
			break;
		}
	}
	if (freeSlot < 0) {
		display->displayPopup("MACRO SLOTS FULL");
		return;
	}
	Macros::changeTargetDestination(clip, heldMacro_, freeSlot, (uint8_t)destination);
	lastSlot_ = freeSlot;
	secondLayer_ = addingSecond;
	lastX_ = x;
	lastY_ = y;
	uiNeedsRendering(getRootUI(), 0xFFFFFFFF, 0);
	showReadout();
}

// On a pad tap / selection change, show the selected target's "<destination> / From - To" readout and
// seed the knob rings, so the same feedback the macro-lane view gives on target-button PRESS appears
// immediately - not only once the gold knob is first turned.
void MacroAssignOverlay::showReadout() {
	if (lastSlot_ < 0 || heldMacro_ < 0) {
		return;
	}
	Output* instrument = Macros::macroHost(getCurrentClip());
	if (instrument == nullptr) {
		return;
	}
	uint8_t destination = instrument->macros[heldMacro_].targets[lastSlot_].destination;
	Macros::showTargetRangeReadout(instrument, heldMacro_, lastSlot_, destination, false);
	Macros::showTargetKnobIndicators(instrument, heldMacro_, lastSlot_);
}

void MacroAssignOverlay::deleteSelectedTarget() {
	if (!active() || lastX_ < 0) {
		return; // nothing selected yet
	}
	Clip* clip = getCurrentClip();
	Output* instrument = Macros::macroHost(clip);
	if (instrument == nullptr) {
		return;
	}
	Macros::Domain domain = Macros::domainForOutput(clip->output);
	int32_t primary = Macros::macroDestinationForPad(domain, lastX_, lastY_, false);
	int32_t second = Macros::macroDestinationForPad(domain, lastX_, lastY_, true);
	Macros::Macro& macro = instrument->macros[heldMacro_];
	Macros::LayerAssignment la = Macros::layerAssignment(macro, primary, second);
	bool primaryAssigned = la.primarySlot >= 0;
	bool secondAssigned = la.secondSlot >= 0;
	// Remove one layer's assignment. If both are assigned, remove the current-layer one (leaving the
	// other, so the pad drops to that layer's colour); if only one is assigned, remove it (pad -> grey).
	int32_t toDelete = -1;
	if (primaryAssigned && secondAssigned) {
		toDelete = secondLayer_ ? second : primary;
		secondLayer_ = !secondLayer_; // the other layer remains selected
	}
	else if (primaryAssigned) {
		toDelete = primary;
		secondLayer_ = false;
	}
	else if (secondAssigned) {
		toDelete = second;
		secondLayer_ = false;
	}
	if (toDelete < 0) {
		return; // this pad isn't assigned to the macro - nothing to delete
	}
	for (int32_t s = 0; s < Macros::kNumTargetSlots; s++) {
		if (macro.targets[s].destination == (uint8_t)toDelete) {
			Macros::changeTargetDestination(clip, heldMacro_, s, Macros::kNoDestination);
		}
	}
	lastSlot_ = -1;                               // no live slot to cycle now
	cycleOnRelease_ = false;                      // a delete cancels any pending release cycle
	uiNeedsRendering(getRootUI(), 0xFFFFFFFF, 0); // pad shows the remaining layer's colour, or grey if none left
}

// A gold-knob turn while the picker is up and a target slot is selected: shape THAT target's From/To
// (knob 0 = From, knob 1 = To), mirroring the macro-lane view's held-target editing - same clamp,
// same re-bake, same "destination name / From - To" readout and knob-ring feedback.
void MacroAssignOverlay::handleModEncoder(int32_t whichModEncoder, int32_t offset) {
	Clip* clip = getCurrentClip();
	Output* instrument = Macros::macroHost(clip);
	if (instrument == nullptr || heldMacro_ < 0 || lastSlot_ < 0) {
		return;
	}
	Macros::editTargetEndpoint(clip, instrument, heldMacro_, lastSlot_, whichModEncoder, offset);
	uint8_t destination = instrument->macros[heldMacro_].targets[lastSlot_].destination;
	Macros::showTargetRangeReadout(instrument, heldMacro_, lastSlot_, destination, false);
	Macros::showTargetKnobIndicators(instrument, heldMacro_, lastSlot_);
}
