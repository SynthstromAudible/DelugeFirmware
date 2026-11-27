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

#include "gui/views/automation/editor_layout/note/velocity_edit.h"

/// Folded velocity editor, toggled with affect entire in the velocity editor. Every note row in the clip is shown in
/// one velocity lane, so a pad press edits all notes that start on a step regardless of which row they are in.
/// A kit's fold state is its affect entire state, so the gold knobs edit the whole kit while folded. Other clips'
/// affect entire is always on, so they keep their own flag.
class VelocityFold : public AutomationEditorLayout {
public:
	bool isFolded();
	/// call after affect entire was pressed in the note editor (and the kit's affect entire toggled)
	void affectEntireChanged();

	void render(InstrumentClip* clip, RGB image[][kDisplayWidth + kSideBarWidth],
	            uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]);
	void padAction(InstrumentClip* clip, int32_t x, int32_t y, int32_t velocity);

	bool anyPadPresses() const { return numPadPresses > 0; }
	/// fine tune the velocity of every note on the held steps with the horizontal encoder
	void adjustHeldVelocity(InstrumentClip* clip, int32_t offset);
	void endPadPresses();

private:
	struct PadPress {
		bool isActive;
		int32_t xDisplay;
		int32_t yDisplay;
		int32_t startPos;
		int32_t endPos;
	};

	void padRelease(int32_t x, int32_t y);
	void setStepVelocity(InstrumentClip* clip, Action* action, int32_t x, int32_t targetVelocity);

	bool foldedNonKit{false};
	PadPress padPresses[kEditPadPressBufferSize]{};
	int32_t numPadPresses{0};
	velocity_edit::Ramp ramp;
};
