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
#include <hid/buttons.h>
#include <stdint.h>

namespace deluge::gui::ui::keyboard::layout {

void KeyboardLayoutVelocityDrums::evaluatePads(PressedPad presses[kMaxNumKeyboardPadPresses]) {
	currentNotesState = NotesState{}; // Erase active notes

	static_assert(kMaxNumActiveNotes < 32, "We use a 32-bit integer to represent note active state");
	uint32_t active_notes = 0;
	std::array<int32_t, kMaxNumActiveNotes> note_on_times{};
	uint32_t offset = getState().drums.scroll_offset;
	uint32_t zoom_level = getState().drums.zoom_level;
	uint32_t edge_size_x = zoom_arr[zoom_level][0];
	uint32_t edge_size_y = zoom_arr[zoom_level][1];
	uint32_t pads_per_row = kDisplayWidth / edge_size_x;
	uint32_t highest_clip_note = getHighestClipNote();
	bool odd_pad = (edge_size_x % 2 == 1 && edge_size_x > 1);
	for (int32_t idx_press = 0; idx_press < kMaxNumKeyboardPadPresses; ++idx_press) {
		if (!presses[idx_press].active) {
			continue;
		}
		uint32_t x = presses[idx_press].x;
		if (x >= kDisplayWidth) {
			continue; // prevents the sidebar from being able to activate notes
		}
		bool x_adjust_note = (odd_pad && x == kDisplayWidth - 1);
		uint32_t y = presses[idx_press].y;
		uint32_t note = (x / edge_size_x) - x_adjust_note + ((y / edge_size_y) * pads_per_row) + offset;
		if (note > highest_clip_note) {
			continue; // save calculation time if press was on an unlit pad. Is there a way to prevent
			          // re-rendering?
		}
		uint32_t velocity =
		    (velocityFromCoords(x, y, edge_size_x, edge_size_y) >> 1); // uses bitshift to divide by two, to get 0-127
		// D_PRINTLN("note, velocity: %d, %d", note, velocity);
		auto note_on_idx = currentNotesState.enableNote(note, velocity);

		if ((active_notes & (1 << note_on_idx)) == 0) {
			active_notes |= 1 << note_on_idx;
			note_on_times[note_on_idx] = presses[idx_press].timeLastPadPress;
		}
		else {
			// this is a retrigger on the same note, see if we should update the velocity
			auto last_on_time = note_on_times[note_on_idx];
			auto this_on_time = presses[idx_press].timeLastPadPress;
			if (util::infinite_a_lt_b(last_on_time, this_on_time)) {
				// this press is more recent, use its velocity.
				currentNotesState.notes[note_on_idx].velocity = velocity;
				note_on_times[note_on_idx] = this_on_time;
			}
		}

		// if this note was recently pressed, set it as the selected drum
		if (isShortPress(note_on_times[note_on_idx])) {
			InstrumentClip* clip = getCurrentInstrumentClip();
			Kit* thisKit = (Kit*)clip->output;
			Drum* thisDrum = thisKit->getDrumFromNoteCode(clip, note);
			bool shouldSendMidiFeedback = false;
			instrumentClipView.setSelectedDrum(thisDrum, true, nullptr, shouldSendMidiFeedback);
		}
	}
}

void KeyboardLayoutVelocityDrums::handleVerticalEncoder(int32_t offset) {
	PressedPad pressedPad{};
	uint32_t edge_size_x = zoom_arr[getState().drums.zoom_level][0];
	handleHorizontalEncoder(offset * (kDisplayWidth / edge_size_x), false, &pressedPad, false);
}

void KeyboardLayoutVelocityDrums::handleHorizontalEncoder(int32_t offset, bool shiftEnabled,
                                                          PressedPad presses[kMaxNumKeyboardPadPresses],
                                                          bool encoderPressed) {
	KeyboardStateDrums& state = getState().drums;
	bool zoom_level_changed = false;
	if (shiftEnabled || Buttons::isButtonPressed(hid::button::X_ENC)) { // zoom control
		if (state.zoom_level + offset >= k_min_zoom_level && state.zoom_level + offset <= k_max_zoom_level) {
			state.zoom_level += offset;
			zoom_level_changed = true;
		}
		else
			return;

		DEF_STACK_STRING_BUF(buffer, 16);
		if (display->haveOLED()) {
			buffer.append("Zoom Level: ");
		}
		buffer.appendInt(state.zoom_level + 1);
		display->displayPopup(buffer.c_str());

		offset = 0; // Since the offset variable can be used for scrolling or zooming,
		            // we reset it to 0 before calculating scroll offset
	} // end zoom control
	// scroll offset control - need to run to adjust to new max position if zoom level has changed

	// Calculate highest possible displayable note with current edge size
	uint32_t edge_size_x = zoom_arr[state.zoom_level][0];
	uint32_t edge_size_y = zoom_arr[state.zoom_level][1];
	int32_t displayed_full_pads_count = ((kDisplayHeight / edge_size_y) * (kDisplayWidth / edge_size_x));
	int32_t highest_scrolled_note = std::max<int32_t>(0, (getHighestClipNote() + 1 - displayed_full_pads_count));

	// Make sure scroll offset value is in bounds.
	// For example, if zoom level has gone down and scroll offset was at max, it will be reduced some.
	int32_t new_offset = std::clamp(state.scroll_offset + offset, getLowestClipNote(), highest_scrolled_note);

	// may need to update the scroll offset if zoom level has changed
	if (new_offset != state.scroll_offset || zoom_level_changed) {
		state.scroll_offset = new_offset;
	}
}

void KeyboardLayoutVelocityDrums::renderPads(RGB image[][kDisplayWidth + kSideBarWidth]) {
	// D_PRINTLN("render pads");
	uint32_t highest_clip_note = getHighestClipNote();
	uint32_t offset = getState().drums.scroll_offset;
	uint32_t offset2 = getCurrentInstrumentClip()->colourOffset + 60;
	uint32_t zoom_level = getState().drums.zoom_level;
	uint32_t edge_size_x = zoom_arr[zoom_level][0];
	uint32_t edge_size_y = zoom_arr[zoom_level][1];
	uint32_t pad_area_1 = edge_size_x * edge_size_y;
	uint32_t pad_area_2 = pad_area_1 + edge_size_y;
	bool odd_pad = (edge_size_x % 2 == 1 && edge_size_x > 1); // check if the view has odd width pads
	// Dims more for smaller pads, less for bigger ones.
	// Changing brightness too much on large areas is unpleasant to the eyes.
	float dim_brightness = std::min(0.25 + 0.65 * pad_area_1 / 128, 0.75);
	// fine tune the curve for different pad sizes.
	float initial_intensity = pad_area_1 == 2 ? 0.25 : pad_area_1 < 6 ? 0.045 : 0.015;
	float intensity_increment_1 = edge_size_x > 1 ? std::exp(-std::log(initial_intensity) / (pad_area_1 - 1)) : 1;
	float intensity_increment_2 = odd_pad ? std::exp(-std::log(initial_intensity) / (pad_area_2 - 1)) : 1;
	uint32_t pads_per_row = kDisplayWidth / edge_size_x;
	uint32_t pads_per_col = kDisplayHeight / edge_size_y;
	uint32_t note = offset;
	for (uint32_t padY = 0; padY < pads_per_col; ++padY) {
		uint32_t y = padY * edge_size_y;
		for (uint32_t padX = 0; padX < pads_per_row; ++padX) {
			uint32_t x = padX * edge_size_x;
			RGB note_colour = RGB::fromHue((note * 14 + (note % 2 == 1) * 107 + offset2) % 192);
			bool note_enabled = currentNotesState.noteEnabled(note);
			// Dim the active notes
			// Brighter by default makes the pads more visible in daylight and allows for the full gradient.
			float brightness_factor = note_enabled ? dim_brightness : 1;

			if (edge_size_x > 1) {
				bool disabled_pad = (note > highest_clip_note);
				bool x_adjust = (odd_pad && padX == pads_per_row - 1);
				uint32_t edge_size_x2 = edge_size_x + x_adjust;
				uint32_t pad_area = x_adjust ? pad_area_2 : pad_area_1;
				float intensity_increment = x_adjust ? intensity_increment_2 : intensity_increment_1;
				float colour_intensity = initial_intensity;
				uint32_t x2 = 0;
				uint32_t y2 = 0;
				for (int32_t i = 0; i < pad_area; ++i) {
					if (disabled_pad)
						image[y + y2][x + x2] = colours::black;
					else {
						image[y + y2][x + x2] =
						    note_colour.transform([brightness_factor, colour_intensity](uint8_t chan) {
							    return (chan * brightness_factor * colour_intensity);
						    });
					}
					x2++;
					if (x2 == edge_size_x2) {
						x2 = 0;
						y2++;
					}
					if (disabled_pad || i == pad_area - 1)
						colour_intensity = 1;
					else
						colour_intensity *= intensity_increment;
				}
			}
			else {
				if (note > highest_clip_note)
					image[y][x] = colours::black;
				else if (note_enabled)
					image[y][x] =
					    note_colour.transform([brightness_factor](uint8_t chan) { return (chan * brightness_factor); });
				else
					image[y][x] = note_colour;
			}
			note++;
		}
	}
}

}; // namespace deluge::gui::ui::keyboard::layout
