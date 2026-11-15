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

#include "model/fx/stutter_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/view.h"

#include <array>
#include <cstdint>

namespace deluge::model::stutter_ui {

namespace {
/// The UI modes a stutter begins in, zero-terminated as isUIModeWithinRange reads them.
constexpr std::array<uint32_t, 5> kStutterUIModes{UI_MODE_CLIP_PRESSED_IN_SONG_VIEW, UI_MODE_HOLDING_ARRANGEMENT_ROW,
                                                  UI_MODE_HOLDING_ARRANGEMENT_ROW_AUDITION, UI_MODE_AUDITIONING, 0};
} // namespace

bool mayBegin() {
	return isUIModeWithinRange(kStutterUIModes.data());
}

void began(ParamManagerForTimeline* paramManager) {
	// Really only for a quantized stutter, but a free one's redraw does no harm.
	view.notifyParamAutomationOccurred(paramManager);
	enterUIMode(UI_MODE_STUTTERING);
}

void ended(ParamManagerForTimeline* paramManager) {
	if (paramManager != nullptr) {
		view.notifyParamAutomationOccurred(paramManager);
	}
	exitUIMode(UI_MODE_STUTTERING);
}

} // namespace deluge::model::stutter_ui
