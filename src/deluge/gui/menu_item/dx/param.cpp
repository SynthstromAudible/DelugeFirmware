/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "param.h"
#include "algorithm_bitmaps.h"
#include "dsp/dx/dx7note.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "hid/buttons.h"
#include "hid/display/oled.h"
#include "model/clip/instrument_clip.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include <algorithm>
#include <cstring>

namespace deluge::gui::menu_item {

DxParam dxParam{l10n::String::STRING_FOR_DX_PARAM};

void DxParam::beginSession(MenuItem* navigatedBackwardFrom) {
	readValueAgain();
}

#define MAX_PARAM_IDX 143

int DxParam::getValue() {
	if (param >= 0 && param <= MAX_PARAM_IDX) {
		return patch->params[param];
	}
	else if (param == -1) {
		return patch->random_detune;
	}
	else {
		return 0;
	}
}

void DxParam::setValue(int val) {
	if (param >= 0 && param <= MAX_PARAM_IDX) {
		patch->params[param] = val;
	}
	else if (param == -1) {
		patch->random_detune = val;
	}

	// NOTE: we don't use currentSource as currently only OSC1 can be DX7
	soundEditor.currentSound->sources[0].dxPatchChanged = true;
}

// inclusive limits, per operator
static int upper_limit_op_param[21] = {
    99, 99, 99, 99, 99, 99, 99, 99, // 0-7: envelope
    99, 99, 99, 3,  3,              // 8 - 12: curve
    7,  3,  7,                      // 13-15: scaling and sensivitity
    99,                             // 16: level
    1,                              // 17: mode: ratio/fixed
    31, 99, 14                      // 18-20: tuning
};

static const int kNumGlobalParams = 18;
static int upper_limit_global_param[kNumGlobalParams] = {
    // starts at 6*21 = 126
    99, 99, 99, 99, 99, 99, 99, 99, // 126-133: pitch envelope
    31,                             // 134: algorithm
    7,                              // 135: feedback
    1,                              // 136: sync
    99, 99, 99, 99,                 // 137-140: LFO speed, delay, pmd, amd
    1,                              // 141: lfo sync,
    4,                              // 142: lfo waveform,
    7,                              // lfo mod sense pitch
};

void DxParam::readValueAgain() {
	patch = soundEditor.currentSound->sources[0].ensureDxPatch();
	displayValue = getValue();
	flash_row = 1;

	int x = -1, y = -1;

	upper_limit = 99;
	// Six operators, each with 21 parameters; global parameters follow at index 126.
	int op = param / 21;
	int idx = param % 21;
	if (0 <= param && param < 6 * 21) {
		upper_limit = upper_limit_op_param[idx];

		y = 7 - op;
		if (idx < 8) {
			// Operator envelope rates 1–4 (0–3) and levels 1–4 (4–7).
			x = idx;
		}
		else if (idx >= 16) {
			// Output level (16), mode (17), coarse (18), fine (19), and detune (20).
			x = (idx - 16) + 8;
		}
		else if (idx >= 13) {
			// Velocity, amp mod, and rate scale occupy the last three columns.
			x = 28 - idx;
		}
		flash_row = 7 - op;
	}
	else if (param >= 6 * 21 && idx < kNumGlobalParams) {
		// global param
		upper_limit = upper_limit_global_param[idx];
	}

	blink_next = false;

	if (param >= 6 * 21 && param < 6 * 21 + 11) {
		// Pitch envelope rates/levels (0–7), algorithm (8), feedback (9), and oscillator sync (10).
		y = 1;
		x = param - 6 * 21;
	}
	else if (param >= 6 * 21 + 11 && param < 6 * 21 + 11 + 7) {
		// LFO rate, delay, pitch depth, amp depth, sync, waveform (11–16), and pitch sensitivity (17).
		y = 0;
		x = param - 6 * 21 - 11;
	}
	else if (param == -1) {
		// Random detune, stored separately from the DX7 parameter array.
		y = 0;
		x = 7;
	}

	drawValue();
	if (x >= 0 && y >= 0) {
		// TODO: different color!
		soundEditor.setupShortcutBlink(x, y, 1);
		soundEditor.blinkShortcut();
	}
	else {
		uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);
	}

	blinkSideColumn();
}

bool DxParam::hasSideColumn() {
	// TODO: check if dx column is really active
	return getRootUI() == &keyboardScreen;
}

void DxParam::blinkSideColumn() {
	if (hasSideColumn()) {
		uiNeedsRendering(&keyboardScreen, 0, 0xFFFFFFFF);
		blink_next = !blink_next;
		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, blink_next ? 100 : 300);
	}
	else {
		uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
	}
}
ActionResult DxParam::timerCallback() {
	blinkSideColumn();
	return ActionResult::DEALT_WITH;
}

void DxParam::selectEncoderAction(int32_t offset) {
	int value = getValue();

	int newval = value + offset;
	if (newval > upper_limit) {
		newval = upper_limit;
	}
	else if (newval < 0) {
		newval = 0;
	}

	setValue(newval);
	displayValue = newval;

	drawValue();
}

void DxParam::horizontalEncoderAction(int32_t offset) {
	if (Buttons::isShiftButtonPressed()) {
		if (param < 0 || param > 6 * 21 || !patch)
			return; // TODO: remember last OP param?
		int cur_op = param / 21;
		int next_op = cur_op;
		do {
			next_op += offset;
			if (next_op >= 6) {
				next_op = 0;
			}
			else if (next_op < 0) {
				next_op = 5;
			}
		} while (!patch->opSwitch(next_op) && next_op != cur_op);

		param = (param % 21) + 21 * next_op;
	}
	else {
		param = param + offset;
		if (param < -1) {
			param = 143;
		}
		else if (param > 143) {
			param = -1;
		}
	}

	readValueAgain();

	if (display->have7SEG()) {
		flashParamName();
	}
}

bool DxParam::potentialShortcutPadAction(int32_t x, int32_t y, bool on) {
	int found_param = -2;
	if (y > 1 && x < kDisplayWidth) {
		int op = 8 - y - 1; // op 0-5
		int idx;
		if (x < 8) {
			idx = x;
		}
		else if (x < 13) {
			idx = (x - 8) + 16;
		}
		else {
			// Columns 14–16: velocity sensitivity, amp mod sensitivity, rate scaling.
			idx = 28 - x;
		}
		found_param = 21 * op + idx;
	}
	else if (y == 1) {
		if (x < 11) {
			found_param = 6 * 21 + x;
		}
	}
	else { // y == 0
		if (x < 7) {
			found_param = 6 * 21 + 11 + x;
		}
		else if (x == 7) {
			found_param = -1;
		}
	}

	if (found_param >= -1) {
		param = found_param;
		readValueAgain();
		if (display->have7SEG()) {
			flashParamName();
		}
	}

	// even if we didn't find param, we want to return true so that we ignore other non-dx shortcut presses
	// while in the dx editor, otherwise you get taken out of the dx editor into the sound editor and it's confusing
	return true;
}

const char* desc_op_long[] = {"env rate1",   "env rate2",  "env rate3",  "env rate4",     "env level1",  "env level2",
                              "env level3",  "env level4", "breakpoint", "left depth",    "right depth", "left curve",
                              "right curve", "rate scale", "ampmod",     "velocity sens", "level",       "mode",
                              "coarse",      "fine",       "detune"};
const char* desc_op_short[] = {"rat1",   "rat2",      "rat3",     "rat4",     "lvl1",     "lvl2",     "lvl3",
                               "lvl4",   "brkp",      "le depth", "ri depth", "le curve", "ri curve", "rate scale",
                               "ampmod", "velo sens", "levl",     "mode",     "coar",     "fine",     "detune"};

const char* desc_global_long[]{"DX7 pitch R1", "DX7 pitch R2",     "DX7 pitch R3",  "DX7 pitch R4",  "DX7 pitch l1",
                               "DX7 pitch l2", "DX7 pitch l3",     "DX7 pitch l4",  "algorithm",     "DX7 feedback",
                               "DX7 osc Sync", "DX7 LFO rate",     "DX7 LFO delay", "DX7 LFO pitch", "DX7 LFO amp",
                               "DX7 LFO sync", "DX7 LFO waveform", "DX7 pitch sens"};

const char* desc_global_short[]{"piR1",       "piR2",      "piR3",    "piR4",     "pil1",     "pil2",
                                "pil3",       "pil4",      "algo",    "fdbk",     "oscSync",  "LFO rate",
                                " LFO delay", "LFO pitch", "LFO amp", "LFO sync", "LFO wave", "pitch sens"};

const char* curves[]{"lin-", "exp-", "exp+", "lin+", "????"};
const char* shapes_long[]{"tri", "saw down", "saw up", "square", "sin", "s-hold"};
const char* shapes_short[]{"tri", "sawd", "sawu", "sqre", "sin", "shld"};

[[nodiscard]] std::string_view DxParam::getTitle() const {
	static char buffer[25];

	if (param < 0) {
		return "random detune";
	}

	int op = param / 21;
	int idx = param % 21;
	if (param < 6 * 21) {

		strcpy(buffer, "op0 ");
		buffer[2] = '6' - op;

		const char* desc;
		if (idx < 8) {
			desc = "envelope";
		}
		else if (idx < 13) {
			desc = "scaling";
		}
		else if (idx < 16) {
			desc = "params";
		}
		else {
			desc = "tune/level";
		}

		strcpy(buffer + 4, desc);

		return buffer;
	}
	else if (param < 6 * 21 + 8) {
		return "dx7 pitch env";
	}
	else if (param < 6 * 21 + 18) {
		return desc_global_long[param - 6 * 21];
	}

	return "DX7 PARAM";
}

void DxParam::flashParamName() {
	if (0 <= param && param < 6 * 21) {
		char buf[12] = {0};
		int op = param / 21;
		int idx = param % 21;
		buf[0] = 'o';
		buf[1] = '6' - op;
		if (idx < 4) {
			buf[2] = 'r';
			buf[3] = '1' + idx;
		}
		else if (idx < 8) {
			buf[2] = 'l';
			buf[3] = '1' + (idx - 4);
		}
		else {
			if (hasSideColumn()) {
				strcpy(buf, desc_op_short[idx]);
			}
			else {
				buf[2] = ' ';
				strcpy(buf + 3, desc_op_short[idx]);
			}
		}
		display->setScrollingText(buf, 0, 600, 1);
	}
	else if (param >= 6 * 21 && param < 6 * 21 + 18) {
		display->setScrollingText(desc_global_short[param - 6 * 21], 0, 600, 1);
	}
	else {
		display->setScrollingText(getTitle().begin(), 0, 600, 1);
	}
}

using deluge::hid::display::OLED;
static void show(const char* text, int r, int c, bool inv = false) {
	int ybel = 7 + (2 + r) * (kTextSizeYUpdated + 2);
	int xpos = 5 + c * kTextSpacingX;
	OLED::main.drawString(text, xpos, ybel, kTextSpacingX, kTextSizeYUpdated);
	if (inv) {
		int width = strlen(text);
		OLED::main.invertArea(xpos - 1, kTextSpacingX * width + 1, ybel - 1, ybel + kTextSizeYUpdated);
	}
}

static void show(int val, int r, int c, bool inv = false) {
	char buffer[12];
	intToString(val, buffer, 2);
	show(buffer, r, c, inv);
}

static void renderEnvelope(uint8_t* params, int op, int idx) {
	show("rate", 0, 0, false);
	show("levl", 1, 0, false);
	for (int i = 0; i < 4; i++) {
		int val = params[op * 21 + i];
		show(val, 0, 5 + i * 3, (i == idx));
		val = params[op * 21 + 4 + i];
		show(val, 1, 5 + i * 3, (i + 4 == idx));
	}
}

static void renderScaling(uint8_t* params, int op, int idx) {
	char buffer[12];

	for (int i = 0; i < 2; i++) {
		int val = params[op * 21 + 9 + i];
		show(val, 0, 1 + i * 11, (9 + i == idx));

		val = params[op * 21 + 11 + i];
		show(curves[std::min(val, 4)], 1, 1 + i * 11, (11 + i == idx));
	}

	int ybelmid = 7 + 2 * (kTextSizeYUpdated + 2) + ((kTextSizeYUpdated + 1) >> 1);
	int val = params[op * 21 + 8];
	intToString(val, buffer, 2);
	int xpos = 14 + 6 * kTextSpacingX;
	OLED::main.drawString(buffer, xpos, ybelmid, kTextSpacingX, kTextSizeYUpdated);
	if (8 == idx) {
		OLED::main.invertArea(xpos - 1, kTextSpacingX * 2 + 1, ybelmid - 1, ybelmid + kTextSizeYUpdated);
	}

	int noteCode = val + 17;
	ui::keyboard::KeyboardState& state = getCurrentInstrumentClip()->keyboardState;
	if (getRootUI() == &keyboardScreen && state.currentLayout == KeyboardLayoutType::KeyboardLayoutTypeIsomorphic) {
		int notedisp = noteCode - state.isomorphic.scrollOffset;
		int x = notedisp;
		int y = 0;
		while (x > 16 && y < 7) {
			x -= state.isomorphic.rowInterval;
			y += 1;
		}
		if (x > 10 && y < 7 && state.isomorphic.rowInterval <= 7) {
			x -= state.isomorphic.rowInterval;
			y += 1;
		}

		if (x >= 0 && x < 16) {
			// TODO: different color than the main shortcut blink? or at least different frequency
			soundEditor.setupShortcutBlink(x, y, 2);
			soundEditor.blinkShortcut();
		}
		else {
			uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);
		}
	}
}

static void renderSensParams(uint8_t* params, int op, int idx) {
	show("rate scale", 0, 1);
	show(params[op * 21 + 13], 0, 12, idx == 13);

	show("ampmod", 1, 0);
	show(params[op * 21 + 14], 1, 7, idx == 14);

	show("velo", 1, 11);
	show(params[op * 21 + 15], 1, 16, idx == 15);
}

static void renderTuning(uint8_t* params, int op, int param) {
	int mode = params[op * 21 + 17];
	const char* text = mode ? "fixed" : "ratio";
	show(text, 0, 1, (17 == param));

	for (int i = 0; i < 3; i++) {
		int val = params[op * 21 + i + 18];
		if (i + 18 == 20)
			val -= 7;
		show(val, 0, 7 + i * 3, (i + 18 == param));
	}

	show("level", 1, 1);

	int val = params[op * 21 + 16];
	show(val, 1, 7, (16 == param));
}

static void renderLFO(uint8_t* params, int param) {
	char buffer[12];
	int ybel = 5 + 2 * (kTextSizeYUpdated + 2) + 2;
	int ybel2 = ybel + (kTextSizeYUpdated + 2);

	for (int i = 0; i < 2; i++) {
		int val = params[137 + i];
		show(val, 0, 1 + i * 3, (137 + i == param));
	}

	for (int i = 0; i < 2; i++) {
		int val = params[139 + i];
		intToString(val, buffer, 2);
		int xpre = 10 + (i * 9) * kTextSpacingX;
		int xpos = xpre + 6 * kTextSpacingX;
		const char* text = (i == 0) ? "pitch" : "  amp";
		show(text, 1, 1 + i * 9);
		show(val, 1, 1 + i * 9 + 6, (i + 139 == param));
	}

	const char* sync = (params[141] ? "sync" : "    ");
	show(sync, 0, 7, (141 == param));
	int shap = std::min((int)params[142], 5);
	show(shapes_long[shap], 0, 12, (142 == param));

	int val = params[143];
	intToString(val, buffer, 1);
	show(buffer, 1, 10, (143 == param));
}

static void renderAlgorithm(uint8_t* params) {
	int algorithm = std::min<int>(params[134], dx::kNumAlgorithms - 1);

	// Inverted "#NN" badge at the right of the title bar
	char buffer[12] = "#";
	intToString(algorithm + 1, buffer + 1, 2);
	constexpr int32_t kBadgePadding = 2;
	int32_t textWidth = OLED::main.getStringWidthInPixels(buffer, kTextTitleSizeY);
	int32_t badgeX = OLED_MAIN_WIDTH_PIXELS - textWidth - 2 * kBadgePadding;
	OLED::main.drawString(buffer, badgeX + kBadgePadding, OLED_MAIN_TOPMOST_PIXEL + 1, kTextTitleSpacingX,
	                      kTextTitleSizeY);
	OLED::main.invertArea(badgeX, OLED_MAIN_WIDTH_PIXELS - badgeX, OLED_MAIN_TOPMOST_PIXEL, kScreenTitleSeparatorY - 2);

	const dx::AlgorithmBitmap& bitmap = dx::kAlgorithmBitmaps[algorithm];
	OLED::main.drawGraphicMultiLine(&dx::kAlgorithmBitmapData[bitmap.offset], bitmap.x, bitmap.y, bitmap.width,
	                                bitmap.height, bitmap.numBytesTall);
}

void DxParam::drawPixelsForOled() {
	const int y0 = 20;
	char buffer[12];

	if (param < 0 || param == 135 || param == 136) {
		int val = getValue();
		intToString(val, buffer, param < 0 ? 2 : 1);
		OLED::main.drawString(buffer, 50, y0, kTextHugeSpacingX, kTextHugeSizeY);
		return;
	}

	int op = param / 21;
	int idx = param % 21;
	if (param < (6 * 21 + 8) && idx < 8) {
		renderEnvelope(patch->params, op, idx); // op== 6 for pitch envelope
	}
	else if (param < 6 * 21 && idx < 13) {
		renderScaling(patch->params, op, idx);
	}
	else if (param < 6 * 21 && idx < 16) {
		renderSensParams(patch->params, op, idx);
	}
	else if (param < 6 * 21 && idx < 21) {
		renderTuning(patch->params, op, idx);
	}
	else if (param == 134) {
		renderAlgorithm(patch->params);
	}
	else if (param >= 137 && param < 144) {
		renderLFO(patch->params, param);
	}
}

void DxParam::drawValue() {
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		int op = param / 21;
		int idx = param % 21;
		int val = getValue();
		const char* text = NULL;
		if (param < 6 * 21 && (idx == 17)) {
			text = val ? "fixd" : "rati";
		}
		else if (param < 6 * 21 && (idx == 11 || idx == 12)) {
			text = curves[std::min(val, 4)];
		}
		else if (param == 142) {
			int shap = std::min(val, 5);
			text = shapes_short[shap];
		}
		else if (param == 6 * 21 + 8) {
			val += 1; // algorithms start at one
		}
		else if (param < 6 * 21 && (idx == 20)) {
			val -= 7; // detuning -7 - 7
		}
		if (text) {
			display->setText(text);
		}
		else {
			display->setTextAsNumber(val);
		}
	}
}

void DxParam::openForOpOrGlobal(int op) {
	bool was_focused = true; // was already in DxParam
	if (!isUIOpen(&soundEditor) || soundEditor.getCurrentMenuItem() != this) {
		bool success = soundEditor.setup(getCurrentClip(), this, 0);
		if (!success) {
			return;
		}
		soundEditor.enterOrUpdateSoundEditor(true);
		was_focused = false;
	}

	int new_param = op < 6 ? op * 21 + 16 : 134;
	bool flash = true;
	if (was_focused) {
		if (param == new_param) {
			new_param = op < 6 ? op * 21 + 18 : 135;
		}
		else if (0 <= param && param < 6 * 21 + 8) {
			// tricky: we allow to go from op env to pitch env and back
			int param_for_op = op * 21 + (param % 21);
			if (param != param_for_op && param_for_op < 6 * 21 + 8) {
				new_param = param_for_op;
				flash = false;
			}
		}
	}
	param = new_param;
	readValueAgain();
	if (flash && display->have7SEG()) {
		flashParamName();
	}
}

} // namespace deluge::gui::menu_item
