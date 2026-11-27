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

#include "dx.h"
#include "dsp/fixed/dx7/algorithms.h"
#include "gui/ui/keyboard/layout/column_controls.h"
#include "hid/buttons.h"
#include "model/edits/setting_edit.h"
#include "processing/dx7/dx_patch.h"
#include "processing/sound/sound_instrument.h"
#include "util/menu_entry.h"
#include "util/sound_menu_open.h"
#include <algorithm>

using namespace deluge::gui::ui::keyboard::layout;

namespace deluge::gui::ui::keyboard::controls {

DxPatch* getCurrentDxPatch() {
	// The column edits a DX7 on the first oscillator, which no sound holds (the DX7 is a sound's engine)
	return nullptr;
}

void DXColumn::renderColumn(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column, KeyboardLayout* layout) {
	DxPatch* patch = getCurrentDxPatch();
	if (!patch) {
		return;
	}
	const int algid = std::min<uint8_t>(patch->params[134], 31); // a song can carry any byte
	const auto& ops = deluge::dsp::fixed::dx7::kAlgorithms[algid];

	const std::optional<int32_t> flash = deluge::gui::ui::dxColumnFlashRow();

	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		int op = 8 - y - 1; // op 0-5
		if (op < 6) {
			if (!patch->opSwitch(op)) {
				image[y][column] = {255, 0, 0};
			}
			else if (deluge::dsp::fixed::dx7::has(ops[op], deluge::dsp::fixed::dx7::Flag::OUT_BUS_ONE)
			         || deluge::dsp::fixed::dx7::has(ops[op], deluge::dsp::fixed::dx7::Flag::OUT_BUS_TWO)) {
				image[y][column] = {0, 128, 255};
			}
			else {
				image[y][column] = {0, 255, 0};
			}
		}
		else {
			image[y][column] = {0, 0, 0};
		}

		if (flash == y) {
			image[y][column] = {255, 255, 255};
		}
	}
}

bool DXColumn::handleVerticalEncoder(int8_t pad, int32_t offset) {
	return false;
};

void DXColumn::handleLeavingColumn(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                   KeyboardLayout* layout) {};

void DXColumn::handlePad(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, PressedPad pad,
                         KeyboardLayout* layout) {
	// evaluatePads() replays every held pad on each pad and encoder event, so acting on the press would re-toggle
	// an op that is still held down. Act on the release instead, which is delivered exactly once per press.
	if (pad.active) {
		return;
	}

	DxPatch* patch = getCurrentDxPatch();
	if (!patch) {
		return;
	}

	int op = 8 - pad.y - 1; // op 0-5
	if (Buttons::isShiftButtonPressed()) {
		(void)deluge::gui::ui::openShellDxColumn(op);
	}
	else if (op < 6) {
		// The column's patch is the current synth's DX7's (getCurrentDxPatch).
		auto* sound = static_cast<SoundInstrument*>(getCurrentInstrument());
		if (std::optional<deluge::instruments::classic::DxPatchState> changed = sound->dx7Patch.state()) {
			changed->params[155] ^=
			    static_cast<uint8_t>(1 << op); // the op switches: one bit per op, as DxPatch::opSwitch reads them
			deluge::edits::dxPatchFor(*sound, *changed);
		}
	}
};
} // namespace deluge::gui::ui::keyboard::controls
