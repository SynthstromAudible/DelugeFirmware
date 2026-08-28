#pragma once
#include "libdeluge/board.h"
#include "shell/profile.h"
#include <cstdint>

namespace deluge::ui::shell {

/// The real Deluge board literal: 18×8 pads (16+2 sidebar columns), 4×9 buttons, 6 encoders, 1 OLED 128×48
/// visible_top 5.
extern const DelugeBoard kDelugeBoard;

/// Deluge LED indices (matching LED enum values under buttons).
enum class DelugeLed : uint8_t {
	AFFECT_ENTIRE = 3,
	SESSION_VIEW = 12,
	CLIP_VIEW = 21,
	SYNTH = 5,
	KIT = 14,
	MIDI = 23,
	CV = 32,
	KEYBOARD = 30,
	SCALE_MODE = 6,
	CROSS_SCREEN_EDIT = 24,
	BACK = 16,
	LOAD = 15,
	SAVE = 33,
	LEARN = 7,
	TAP_TEMPO = 34,
	SYNC_SCALING = 25,
	TRIPLETS = 17,
	PLAY = 35,
	RECORD = 26,
	SHIFT = 8,
	MOD_0 = 1,
	MOD_1 = 10,
	MOD_2 = 19,
	MOD_3 = 28,
	MOD_4 = 2,
	MOD_5 = 11,
	MOD_6 = 20,
	MOD_7 = 29,
};

/// The board profile for the Deluge: button matrix, LED map, encoders, styles, and shortcuts.
/// @return The profile, viewing static storage.
const BoardProfile& deluge_profile();

} // namespace deluge::ui::shell
