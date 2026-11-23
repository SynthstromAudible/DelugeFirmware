/*
 * Copyright © 2026 Synthstrom Audible Limited
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

// helpers shared by the velocity editor (velocity.cpp) and the folded velocity editor (velocity_fold.cpp)

#include "gui/views/automation/editor_layout.h"
#include "hid/display/display.h"
#include "model/model_stack.h"
#include "model/note/note.h"
#include "model/note/note_row.h"

class Action;

namespace velocity_edit {

/// velocity set by pressing a pad in row y
int32_t getVelocityFromY(int32_t y);
/// whether a square whose velocity is velocity lights the pad in row y
bool velocityReachesY(int32_t velocity, int32_t y);

/// fn(note) for every note of noteRow starting in [startPos, endPos)
template <typename F>
void forNotesInRange(NoteRow* noteRow, int32_t startPos, int32_t endPos, F&& fn) {
	int32_t noteI = noteRow->notes.search(startPos, GREATER_OR_EQUAL);
	for (Note* note = noteRow->notes.getElement(noteI); note && note->pos < endPos;
	     note = noteRow->notes.getElement(++noteI)) {
		fn(note);
	}
}

/// relative velocity scaling: when a press edits several notes at once, the highest velocity note goes to
/// targetVelocity and the others are scaled with it so they keep the same ratio to it. Squares with several notes are
/// drawn up to their highest velocity, so the column ends up at the pressed pad.
/// Edits the notes starting in [startPos, endPos) of each note row that forEachRow(fn) passes to
/// fn(modelStackWithNoteRow).
template <typename ForEachRow>
void setRelativeVelocityInRows(Action* action, int32_t startPos, int32_t endPos, int32_t targetVelocity,
                               ForEachRow&& forEachRow) {
	int32_t maxVelocity = 0;
	forEachRow([&](ModelStackWithNoteRow* modelStackWithNoteRow) {
		forNotesInRange(modelStackWithNoteRow->getNoteRow(), startPos, endPos,
		                [&](Note* note) { maxVelocity = std::max<int32_t>(maxVelocity, note->getVelocity()); });
	});
	if (!maxVelocity) {
		return;
	}
	float scale = static_cast<float>(targetVelocity) / static_cast<float>(maxVelocity);

	forEachRow([&](ModelStackWithNoteRow* modelStackWithNoteRow) {
		NoteRow* noteRow = modelStackWithNoteRow->getNoteRow();
		forNotesInRange(noteRow, startPos, endPos, [&](Note* note) {
			int32_t newVelocity = std::lround(static_cast<float>(note->getVelocity()) * scale);
			noteRow->changeNotesAcrossAllScreens(note->pos, modelStackWithNoteRow, action,
			                                     CORRESPONDING_NOTES_SET_VELOCITY,
			                                     std::clamp<int32_t>(newVelocity, 1, 127));
		});
	});
}

/// relative velocity scaling of the notes starting in [startPos, endPos) of one note row
inline void setRelativeVelocity(Action* action, ModelStackWithNoteRow* modelStackWithNoteRow, int32_t startPos,
                                int32_t endPos, int32_t targetVelocity) {
	setRelativeVelocityInRows(action, startPos, endPos, targetVelocity, [&](auto&& fn) { fn(modelStackWithNoteRow); });
}

/// a ramp between two presses in different columns. The first press keeps its current velocity and the second press
/// sets the other end. Stored as if it was entered left to right.
class Ramp : public AutomationEditorLayout {
public:
	void setup(int32_t firstX, int32_t firstVelocity, int32_t x, int32_t y) {
		int32_t newVelocity = getVelocityFromY(y);
		bool firstIsLeft = firstX < x;
		leftX = firstIsLeft ? firstX : x;
		rightX = firstIsLeft ? x : firstX;
		leftVelocity = firstIsLeft ? firstVelocity : newVelocity;
		rightVelocity = firstIsLeft ? newVelocity : firstVelocity;
	}

	/// fn(x, velocity) for each step of the ramp that isStep(x) accepts, with the velocity interpolated linearly from
	/// the left end on the first step to the right end on the last
	template <typename IsStep, typename F>
	void forEachStep(IsStep&& isStep, F&& fn) const {
		int32_t steps[kDisplayWidth];
		int32_t numSteps = 0;
		for (int32_t x = leftX; x <= rightX; x++) {
			if (isStep(x)) {
				steps[numSteps++] = x;
			}
		}
		for (int32_t step = 0; step < numSteps; step++) {
			int32_t velocity = leftVelocity;
			if (numSteps > 1) {
				float fraction = static_cast<float>(step) / static_cast<float>(numSteps - 1);
				velocity += static_cast<int32_t>(std::round((rightVelocity - leftVelocity) * fraction));
			}
			fn(steps[step], std::clamp<int32_t>(velocity, 1, 127));
		}
	}

	/// show the velocity at both ends of the ramp, or on 7seg the end at the pad pressed last (x)
	void renderValues(int32_t x) {
		if (display->haveOLED()) {
			renderDisplay(leftVelocity, rightVelocity);
		}
		else {
			renderDisplay(leftX == x ? leftVelocity : rightVelocity);
		}
	}

private:
	int32_t leftX{0};
	int32_t rightX{0};
	int32_t leftVelocity{0};
	int32_t rightVelocity{0};
};

} // namespace velocity_edit
