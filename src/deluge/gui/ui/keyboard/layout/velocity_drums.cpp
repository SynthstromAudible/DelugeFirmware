/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

#include "gui/ui/keyboard/layout/velocity_drums.h"
#include "hid/display/display.h"
#include "model/instrument/kit.h"
#include "util/functions.h"
#include <stdint.h>

namespace deluge::gui::ui::keyboard::layout {

void KeyboardLayoutVelocityDrums::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	uint8_t noteIdx = 0;

	currentNotesState = NotesState{}; // Erase active notes

	static_assert(kMaxNumActiveNotes < 32, "We use a 32-bit integer to represent note active state");
	uint32_t activeNotes = 0;
	std::array<int32_t, kMaxNumActiveNotes> noteOnTimes{};

	for (int32_t idxPress = 0; idxPress < kMaxNumKeyboardPadPresses; ++idxPress) {
		if (presses[idxPress].active && presses[idxPress].x < kDisplayWidth) {
			uint8_t edgeSizeX = (uint32_t)getState().drums.edgeSizeX;
			uint8_t edgeSizeY = (uint32_t)getState().drums.edgeSizeY;
			uint8_t note = noteFromCoords(presses[idxPress].x, presses[idxPress].y, edgeSizeX, edgeSizeY);

			uint8_t velocity =
			    (velocityFromCoords(presses[idxPress].x, presses[idxPress].y, edgeSizeX, edgeSizeY) >> 1);
			auto noteOnIdx = currentNotesState.enableNote(note, velocity);

			// exceeded maximum number of active notes, ignore this note-on
			if (noteOnIdx == kMaxNumActiveNotes) {
				continue;
			}

			if ((activeNotes & (1 << noteOnIdx)) == 0) {
				activeNotes |= 1 << noteOnIdx;
				noteOnTimes[noteOnIdx] = presses[idxPress].timeLastPadPress;
			}
			else {
				// this is a retrigger on the same note, see if we should update the velocity
				auto lastOnTime = noteOnTimes[noteOnIdx];
				auto thisOnTime = presses[idxPress].timeLastPadPress;
				if (util::infinite_a_lt_b(lastOnTime, thisOnTime)) {
					// this press is more recent, use its velocity.
					currentNotesState.notes[noteOnIdx].velocity = velocity;
					noteOnTimes[noteOnIdx] = thisOnTime;
				}
			}

			// if this note was recently pressed, set it as the selected drum
			if (isShortPress(noteOnTimes[noteOnIdx])) {
				InstrumentClip* clip = getCurrentInstrumentClip();
				Kit* thisKit = (Kit*)clip->output;
				Drum* thisDrum = thisKit->getDrumFromNoteCode(clip, note);
				bool shouldSendMidiFeedback = false;
				instrumentClipView.setSelectedDrum(thisDrum, true, nullptr, shouldSendMidiFeedback);
			}
		}
	}
}

void KeyboardLayoutVelocityDrums::handleVerticalEncoder(int32_t offset) {
	PressedPad pressedPad{};
	handleHorizontalEncoder(offset * (kDisplayWidth / getState().drums.edgeSizeX), false, &pressedPad, false);
}

void KeyboardLayoutVelocityDrums::handleHorizontalEncoder(int32_t offset, bool shiftEnabled,
                                                          PressedPad presses[kMaxNumKeyboardPadPresses],
                                                          bool encoderPressed) {
	KeyboardStateDrums& state = getState().drums;
	bool zoomLevelChanged = false;
	if (shiftEnabled || Buttons::isButtonPressed(hid::button::X_ENC)) { // zoom control
		if (state.zoomLevel + offset >= kMinZoomLevel && state.zoomLevel + offset <= kMaxZoomLevel) {
			state.zoomLevel += offset;
			zoomLevelChanged = true;
		}
		else return;

		state.edgeSizeX = zoomArr[state.zoomLevel][0];
		state.edgeSizeY = zoomArr[state.zoomLevel][1];
		char buffer[14] = "Pad Area:   ";
		auto displayOffset = (display->haveOLED() ? 9 : 0);
		intToString(state.edgeSizeX * state.edgeSizeY, buffer + displayOffset, 1);
		display->displayPopup(buffer);

		offset = 0; // Since the offset variable can be used for scrolling or zooming,
		// we reset it to 0 before calculating scroll offset
	} // end zoom control
	// scroll offset control - need to run to adjust to new max position if zoom level has changed

	// Calculate highest possible displayable note with current edgeSize
	int32_t displayedfullPadsCount = ((kDisplayHeight / state.edgeSizeY) * (kDisplayWidth / state.edgeSizeX));
	int32_t highestScrolledNote = std::max<int32_t>(0, (getHighestClipNote() + 1 - displayedfullPadsCount));

	// Make sure scroll offset value is in bounds.
	// For example, if zoom level has gone down and scroll offset was at max, it will be reduced some.
	int32_t newOffset = std::clamp(state.scrollOffset + offset, getLowestClipNote(), highestScrolledNote);

	// Only need to update colors and screen if the zoom level and/or scroll offset value has changed
	if (newOffset != state.scrollOffset || zoomLevelChanged) {
		state.scrollOffset = newOffset;
		precalculate();
	}
}

void KeyboardLayoutVelocityDrums::precalculate() {
	// precalculate pad colour set for current zoom level and scroll offset position

	KeyboardStateDrums& state = getState().drums;
	// KeyboardStateIsomorphic& state2 = getState().isomorphic;

	// Pre-Buffer colours for next renderings
	int32_t displayedfullPadsCount = ((kDisplayHeight / state.edgeSizeY) * (kDisplayWidth / state.edgeSizeX));
	// RGB noteColours[displayedfullPadsCount];
	// uint8_t hue_step = 100 / std::floor(kDisplayWidth / state.edgeSizeX);
	// int32_t offset2 = state.scrollOffset + state2.scrollOffset; // hue offset adjustment

	int32_t offset = state.scrollOffset;
	D_PRINTLN("offset: %d", offset);
	// int32_t offset2 = getState().isomorphic.scrollOffset;
	// D_PRINTLN("offset2: %d", offset2);
	for (int32_t i = 0; i < displayedfullPadsCount; i++) {
		// noteColours[i] = getNoteColour(state.scrollOffset + i);
		uint32_t i2 = state.scrollOffset + i;
		// int32_t i2 = offset2 + i;
		noteColours[i] = RGB::fromHue((i2 * 14 + (i2 % 2 == 1) * 107) % 192);
	}
}

void KeyboardLayoutVelocityDrums::renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
	uint8_t highestClipNote = getHighestClipNote();
	uint8_t edgeSizeX = getState().drums.edgeSizeX;
	uint8_t edgeSizeY = getState().drums.edgeSizeY;
	uint8_t offset = getState().drums.scrollOffset;
	float padArea = edgeSizeX * edgeSizeY;
	float padArea2 = pow(padArea, 2);
	float dimBrightness = std::min(0.35 + 0.65 * padArea / 128,0.75);
	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		uint8_t localY = y % edgeSizeY;
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			uint8_t note = noteFromCoords(x, y, edgeSizeX, edgeSizeY);
			if (note > highestClipNote) {
				image[y][x] = colours::black;
				continue;
			}

			RGB noteColour = noteColours[note - offset];

			uint8_t localX = x % edgeSizeX;
			float position = localX + (localY * edgeSizeX) + 1;
			float colourIntensity = position * position / padArea2; // use quadratic curve for pad brightness

			// Dim the active notes

			float brightnessFactor = currentNotesState.noteEnabled(note)? dimBrightness : 1;

			image[y][x] = noteColour.transform([colourIntensity, brightnessFactor](uint8_t chan) {
				return (chan * colourIntensity * brightnessFactor);
			});
		}
	}
}

}; // namespace deluge::gui::ui::keyboard::layout
