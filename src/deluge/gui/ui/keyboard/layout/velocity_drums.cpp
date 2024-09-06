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
			uint8_t note = noteFromCoords(presses[idxPress].x, presses[idxPress].y);
			uint8_t velocity = (intensityFromCoords(presses[idxPress].x, presses[idxPress].y) >> 1);
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
				instrumentClipView.setSelectedDrum(thisDrum);
			}
		}
	}
}

void KeyboardLayoutVelocityDrums::handleVerticalEncoder(int32_t offset) {
	PressedPad pressedPad{};
	handleHorizontalEncoder(offset * (kDisplayWidth / getState().drums.edgeSize), false, &pressedPad, false);
}

void KeyboardLayoutVelocityDrums::handleHorizontalEncoder(int32_t offset, bool shiftEnabled,
                                                          PressedPad presses[kMaxNumKeyboardPadPresses],
                                                          bool encoderPressed) {
	KeyboardStateDrums& state = getState().drums;

	if (shiftEnabled) {
		state.edgeSize += offset;
		state.edgeSize = std::clamp(state.edgeSize, kMinDrumPadEdgeSize, kMaxDrumPadEdgeSize);

		char buffer[13] = "Pad size:   ";
		auto displayOffset = (display->haveOLED() ? 10 : 0);
		intToString(state.edgeSize, buffer + displayOffset, 1);
		display->displayPopup(buffer);

		offset = 0; // Reset offset variable for processing scroll calculation without actually shifting
	}

	// Calculate highest possible displayable note with current edgeSize
	int32_t displayedfullPadsCount = ((kDisplayHeight / state.edgeSize) * (kDisplayWidth / state.edgeSize));
	int32_t highestScrolledNote = std::max<int32_t>(0, (getHighestClipNote() + 1 - displayedfullPadsCount));

	// Make sure current value is in bounds
	state.scrollOffset = std::clamp(state.scrollOffset, getLowestClipNote(), highestScrolledNote);

	// Offset if still in bounds (check for verticalEncoder)
	int32_t newOffset = state.scrollOffset + offset;
	if (newOffset >= getLowestClipNote() && newOffset <= highestScrolledNote) {
		state.scrollOffset = newOffset;
	}

	precalculate();
}

void KeyboardLayoutVelocityDrums::precalculate() {
	KeyboardStateDrums& state = getState().drums;

	// Pre-Buffer colours for next renderings
	int32_t displayedfullPadsCount = ((kDisplayHeight / state.edgeSize) * (kDisplayWidth / state.edgeSize));
	for (int32_t i = 0; i < displayedfullPadsCount; ++i) {
		noteColours[i] = getNoteColour(state.scrollOffset + i);
	}
}

void KeyboardLayoutVelocityDrums::renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
	uint8_t highestClipNote = getHighestClipNote();

	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			uint8_t note = noteFromCoords(x, y);
			if (note > highestClipNote) {
				image[y][x] = colours::black;
				continue;
			}

			RGB noteColour = noteColours[note - getState().drums.scrollOffset];

			uint8_t colourIntensity = intensityFromCoords(x, y);

			// Highlight active notes
			uint8_t brightnessDivider = currentNotesState.noteEnabled(note) ? 1 : 3;

			image[y][x] = noteColour.transform([colourIntensity, brightnessDivider](uint8_t chan) {
				return ((chan * colourIntensity / 255) / brightnessDivider);
			});
		}
	}
}

}; // namespace deluge::gui::ui::keyboard::layout
