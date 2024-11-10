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
#include "gui/views/song_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "playback/mode/session.h"

using namespace deluge::gui::ui::keyboard::layout;

namespace deluge::gui::ui::keyboard::controls {

void SessionColumn::renderColumn(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column, KeyboardLayout* layout) {
	bool armed = false;
	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		armed |= view.renderMacros(column, y, -1, image, nullptr);
	}
	if (armed) {
		view.flashPlayEnable();
	}
}

bool SessionColumn::handleVerticalEncoder(int8_t pad, int32_t offset) {
	SongMacro& m = currentSong->songMacros[pad];
	int typeIndex = (int32_t)m.type + offset;
	if (typeIndex >= static_cast<uint8_t>(SongMacroType::NumTypes)) {
		typeIndex = 0;
	}
	else if (typeIndex < 0) {
		typeIndex = static_cast<uint8_t>(SongMacroType::NumTypes) - 1;
	}

	m.type = (SongMacroType)typeIndex;
	m.clip = nullptr;
	m.output = nullptr;
	m.section = 0;

	switch (m.type) {
	case SongMacroType::ClipLaunch:
		m.clip = getCurrentClip();
		break;

	case SongMacroType::OutputCycle:
		m.output = getCurrentOutput();
		break;

	case SongMacroType::SectionLaunch:
		m.section = getCurrentClip()->section;

	default:
		break;
	}

	return true;
};

void SessionColumn::handleLeavingColumn(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                        KeyboardLayout* layout){};

void SessionColumn::handlePad(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, PressedPad pad,
                              KeyboardLayout* layout) {

	if (pad.active) {}
	else {
		view.activateMacro(pad.y);
	}
	view.flashPlayEnable();
};

} // namespace deluge::gui::ui::keyboard::controls
