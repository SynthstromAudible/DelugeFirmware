/*
 * Copyright (c) 2024 Sean Ditny
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
#include "gui/menu_item/integer.h"
#include "gui/menu_item/note/selected_note.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "model/clip/instrument_clip.h"
#include "model/instrument/kit.h"
#include "model/model_stack.h"
#include "model/note/note_row.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::note {
class Velocity final : public SelectedNote {
public:
	using SelectedNote::SelectedNote;

	[[nodiscard]] int32_t getMaxValue() const override { return 127; }
	[[nodiscard]] int32_t getMinValue() const override { return 1; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return NUMBER; }

	/// @brief Begin an editing session with this menu item.
	///
	/// Should make sure the menu's internal state matches the system and redraw the display.
	void beginSession(MenuItem* navigatedBackwardFrom = nullptr) final override { readValueAgain(); }

	/// @brief  read the velocity of the left most note square selected
	/// @details If a note pad is currently being pressed, it will read the velocity from leftmost pad.
	///          If a note pad is released, it will remember it and read the velocity from the leftmost remembered pad
	///			 If there's more than one note within a pad (e.g. blurred), it will display the average velocity
	void readCurrentValue() override {
		// stores pointer to pad press found
		const EditPadPress* selected_press = nullptr;

		// are we currently pressing any note pads?
		for (const auto& press : instrumentClipView.editPadPresses) {
			if (press.isActive && (!selected_press || press.intendedPos < selected_press->intendedPos)) {
				// found left most active note pad press
				selected_press = &press;
			}
		}

		// if we're currently pressing any note pads, get their square
		if (selected_press) {
			const auto& square = instrumentClipView.gridSquareInfo[selected_press->yDisplay][selected_press->xDisplay];
			if (square.isValid) {
				// found left most active note pad square, display it's average velocity
				this->setValue(square.averageVelocity);
				return;
			}
		}

		// we're not pressing any note pads or no valid square found, fall back to remembered edit targets
		auto edit_targets = instrumentClipView.get_note_edit_targets();
		for (const auto& target : edit_targets) {
			if (target.isActive && (!selected_press || target.intendedPos < selected_press->intendedPos)) {
				// found left most remembered note edit target
				selected_press = &target;
			}
		}
		if (selected_press) {
			// found left most active remembered note edit pad target, display it's intended velocity
			// intended velocity is the average if there's more than one note within the pad
			this->setValue(selected_press->intendedVelocity);
		}
	}

	void selectEncoderAction(int32_t offset) final override {
		instrumentClipView.adjustVelocity(offset);
		readValueAgain();
	}

	void writeCurrentValue() override { ; }
};
} // namespace deluge::gui::menu_item::note
