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

/// See `host_script_tables.h` for why this is its own translation unit: it must link into both
/// the host BSP and `deluge_spec` (`tests/spec/harness/tables_spec.cpp`'s cross-check), and
/// `host_script.c` proper cannot -- it pulls in BSP statics. This file depends on nothing beyond
/// the C standard library.

#include "host_script_tables.h"

#include <string.h>

typedef struct HostScriptButton {
	const char* name;
	uint8_t x;
	uint8_t y;
} HostScriptButton;

/// The Deluge's 4 × 9 button matrix, row-major, exactly as `src/shell/profiles/deluge.cpp`'s
/// `kMatrix` spells it (which is `src/definitions_cxx.hpp:59-84` in turn). Names are
/// `shell::button_name()`'s (`src/shell/profile.cpp:39-45`). `ButtonId::None` holes -- (4,0) and
/// (4,2) -- carry no button and so are absent.
static const HostScriptButton k_buttons[] = {
    // y = 0
    {"YEnc", 0, 0},
    {"Mod0", 1, 0},
    {"Mod4", 2, 0},
    {"AffectEntire", 3, 0},
    {"Synth", 5, 0},
    {"Scale", 6, 0},
    {"Learn", 7, 0},
    {"Shift", 8, 0},
    // y = 1
    {"XEnc", 0, 1},
    {"Mod1", 1, 1},
    {"Mod5", 2, 1},
    {"Session", 3, 1},
    {"TempoEnc", 4, 1},
    {"Kit", 5, 1},
    {"Load", 6, 1},
    {"Back", 7, 1},
    {"Triplets", 8, 1},
    // y = 2
    {"ModEnc0", 0, 2},
    {"Mod2", 1, 2},
    {"Mod6", 2, 2},
    {"Clip", 3, 2},
    {"Midi", 5, 2},
    {"CrossScreen", 6, 2},
    {"SyncScaling", 7, 2},
    {"Record", 8, 2},
    // y = 3
    {"ModEnc1", 0, 3},
    {"Mod3", 1, 3},
    {"Mod7", 2, 3},
    {"Keyboard", 3, 3},
    {"SelectEnc", 4, 3},
    {"Cv", 5, 3},
    {"Save", 6, 3},
    {"TapTempo", 7, 3},
    {"Play", 8, 3},
};

/// `EncoderRole` name by firmware encoder index -- the index `deluge_encoder_take_edges()` takes.
///
/// `hid/encoder_input.cpp:56-63` fixes the order (`0=scrollY 1=scrollX 2=tempo 3=select 4=mod1
/// 5=mod0`), and `hid/encoders.h:80-81` fixes which gold knob each `mod` is: `mod1` is the UPPER
/// gold encoder and `mod0` the LOWER. So index 4 is `GoldUpper` and index 5 is `GoldLower` -- the
/// opposite of what a name-order reading of the two enums would suggest.
static const char* const k_encoder_roles[6] = {
    "Vertical",   // 0 = scrollY
    "Horizontal", // 1 = scrollX
    "Tempo",      // 2
    "Select",     // 3
    "GoldUpper",  // 4 = mod1 = upper gold
    "GoldLower",  // 5 = mod0 = lower gold
};

int ui_script_button_xy(const char* name, uint8_t* x, uint8_t* y) {
	if (name == NULL) {
		return 0;
	}
	for (size_t i = 0; i < sizeof(k_buttons) / sizeof(k_buttons[0]); i++) {
		if (strcmp(k_buttons[i].name, name) == 0) {
			if (x != NULL) {
				*x = k_buttons[i].x;
			}
			if (y != NULL) {
				*y = k_buttons[i].y;
			}
			return 1;
		}
	}
	return 0;
}

int ui_script_encoder_index(const char* role) {
	if (role == NULL) {
		return -1;
	}
	for (int i = 0; i < 6; i++) {
		if (strcmp(k_encoder_roles[i], role) == 0) {
			return i;
		}
	}
	return -1;
}

const char* ui_script_button_name_at(uint8_t x, uint8_t y) {
	for (size_t i = 0; i < sizeof(k_buttons) / sizeof(k_buttons[0]); i++) {
		if (k_buttons[i].x == x && k_buttons[i].y == y) {
			return k_buttons[i].name;
		}
	}
	return NULL;
}
