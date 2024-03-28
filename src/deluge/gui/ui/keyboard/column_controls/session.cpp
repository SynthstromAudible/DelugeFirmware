/*
 * Copyright © 2016-2024 Synthstrom Audible Limited
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

#include "session.h"
#include "gui/ui/keyboard/layout/column_controls.h"
#include "hid/buttons.h"
#include "playback/mode/session.h"

using namespace deluge::gui::ui::keyboard::layout;

namespace deluge::gui::ui::keyboard::controls {

void SessionColumn::renderColumn(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column) {
	uint8_t brightness = 1;
	uint8_t otherChannels = 0;

	int32_t num = currentSong->sessionClips.getNumElements();

	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		switch (macros[y].kind) {
			case CLIP_LAUNCH:
				if (macros[y].clip->activeIfNoSolo) {
					image[y][column] = {0, 255, 0};
				} else {
					image[y][column] = {255, 0, 0};
				}

				break;
			case OUTPUT_CYCLE:
				image[y][column] = {0, 64, 255};
				break;
			case SECTION:
				image[y][column] = {255, 0, 128};
				break;
			case NO_MACRO:
				image[y][column] = {0, 0, 0};
		}

	}
}

bool SessionColumn::handleVerticalEncoder(int8_t pad, int32_t offset) {
	SessionMacro &m = macros[pad];
	int kindIndex = (int32_t)m.kind + offset;
	if (kindIndex >= SessionMacroKind::NUM_KINDS) {
		kindIndex = 0;
	} else if (kindIndex < 0) {
		kindIndex = SessionMacroKind::NUM_KINDS - 1;
	}

	m.kind = (SessionMacroKind)kindIndex;

	switch (m.kind) {
		case CLIP_LAUNCH:
			m.clip = getCurrentClip();
			break;

		case OUTPUT_CYCLE:
			m.clip = nullptr;
			m.output = getCurrentOutput();
			break;

		case SECTION:
			m.section = getCurrentClip()->section;

		default:
			break;
	}

	return true;
};

void SessionColumn::handlePad(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, PressedPad pad,
                          KeyboardLayout* layout) {

	SessionMacro &m = macros[pad.y];
	switch (m.kind) {
		case CLIP_LAUNCH:
			if (pad.active) {
				session.toggleClipStatus(m.clip, nullptr, Buttons::isShiftButtonPressed(), kInternalButtonPressLatency);
			}
			else if (!pad.padPressHeld) {
			}
			else {
			}
			break;

		case OUTPUT_CYCLE:
			handleOutput(m, pad);
			break;

		case SECTION:
			// TODO: we can do really fancy stuff like registering a section+output combo
			if (!pad.active) {
				session.armSection(m.section, kInternalButtonPressLatency);
			}

		default:
			break;
	}
};

Clip *SessionColumn::findNextClipForOutput(SessionMacro &m, PressedPad pad) {
	int last_active = -1;
	for (int i = 0; i < currentSong->sessionClips.getNumElements(); i++) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(i);
		if (clip->output == m.output) {
			if (last_active == -1) {
				if(clip->activeIfNoSolo) {
					last_active = i;
				}
			}
			else {
				return clip;
			}
		}
	}

	// might need to cycle around to find the next clip
	for (int i = 0; i < last_active; i++) {
		Clip* clip = currentSong->sessionClips.getClipAtIndex(i);
		if (clip->output == m.output) {
			return clip;
		}
	}


	if (m.clip && m.clip->output == m.output) {
		return m.clip;
	}

	return nullptr;
}

void SessionColumn::handleOutput(SessionMacro &m, PressedPad pad) {
	if (pad.active) {
	}
	else if (!pad.padPressHeld) {
		Clip *nextClip = findNextClipForOutput(m, pad);
		if (nextClip) {
			session.toggleClipStatus(nextClip, nullptr, Buttons::isShiftButtonPressed(), kInternalButtonPressLatency);
		}
	}
	else {
		// long press: mute if possible, otherwise do the same.
		Clip *curClip = currentSong->getClipWithOutput(m.output, true, nullptr);
		if (!curClip) {
			curClip = findNextClipForOutput(m, pad);
		}
		if (curClip) {
			session.toggleClipStatus(curClip, nullptr, Buttons::isShiftButtonPressed(), kInternalButtonPressLatency);
		}
	}

}

} // namespace deluge::gui::ui::keyboard::controls
