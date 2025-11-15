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
#include "dsp/dx/dx7note.h"
#include "gui/menu_item/dx/param.h"
#include "gui/ui/keyboard/layout/column_controls.h"
#include "gui/ui/sound_editor.h"
#include "hid/buttons.h"
#include "processing/sound/sound_instrument.h"

using namespace deluge::gui::ui::keyboard::layout;

namespace deluge::gui::ui::keyboard::controls {

PLACE_SDRAM_TEXT DxPatch* getCurrentDxPatch() {
	auto* inst = getCurrentInstrument();
	if (inst->type == OutputType::SYNTH) {
		auto* sound = (SoundInstrument*)inst;
		if (sound->sources[0].oscType == OscType::DX7) {
			return sound->sources[0].dxPatch;
		}
	}
	return nullptr;
}

PLACE_SDRAM_TEXT void DXColumn::renderColumn(RGB image[][kDisplayWidth + kSideBarWidth], int32_t column,
                                             KeyboardLayout* layout) {
	using menu_item::dxParam;
	DxPatch* patch = getCurrentDxPatch();
	if (!patch) {
		return;
	}
	int algid = patch->params[134];
	FmAlgorithm a = FmCore::algorithms[algid];

	bool is_editing = (getCurrentUI() == &soundEditor && soundEditor.getCurrentMenuItem() == &dxParam);

	for (int32_t y = 0; y < kDisplayHeight; ++y) {
		int op = 8 - y - 1; // op 0-5
		if (op < 6) {
			if (!patch->opSwitch(op)) {
				image[y][column] = {255, 0, 0};
			}
			else if (a.ops[op] & (OUT_BUS_ONE | OUT_BUS_TWO)) {
				image[y][column] = {0, 128, 255};
			}
			else {
				image[y][column] = {0, 255, 0};
			}
		}
		else {
			image[y][column] = {0, 0, 0};
		}

		if (is_editing && dxParam.flash_row == y && dxParam.blink_next) {
			image[y][column] = {255, 255, 255};
		}
	}
}

PLACE_SDRAM_TEXT bool DXColumn::handleVerticalEncoder(int8_t pad, int32_t offset) {
	return false;
};

PLACE_SDRAM_TEXT void DXColumn::handleLeavingColumn(ModelStackWithTimelineCounter* modelStackWithTimelineCounter,
                                                    KeyboardLayout* layout) {};

PLACE_SDRAM_TEXT void DXColumn::handlePad(ModelStackWithTimelineCounter* modelStackWithTimelineCounter, PressedPad pad,
                                          KeyboardLayout* layout) {
	DxPatch* patch = getCurrentDxPatch();
	if (!patch) {
		return;
	}

	int op = 8 - pad.y - 1; // op 0-5
	if (pad.active) {
		if (Buttons::isShiftButtonPressed()) {
			menu_item::dxParam.openForOpOrGlobal(op);
		}
		else {
			if (op < 6) {
				bool val = patch->opSwitch(op);
				patch->setOpSwitch(op, !val);
			}
		}
	}
};
} // namespace deluge::gui::ui::keyboard::controls
