#pragma once

#include "RZA1/system/r_typedefs.h"
#include "definitions.h"

namespace hid {

namespace button {
constexpr uint8_t fromXY(int x, int y) {
	return 9 * (y + displayHeight * 2) + x;
}

// clang-format off
enum class Button : uint8_t {
	AFFECT_ENTIRE     = fromXY(affectEntireButtonX, affectEntireButtonY),
	SESSION_VIEW      = fromXY(sessionViewButtonX, sessionViewButtonY),
	CLIP_VIEW         = fromXY(clipViewButtonX, clipViewButtonY),
	SYNTH             = fromXY(synthButtonX, synthButtonY),
	KIT               = fromXY(kitButtonX, kitButtonY),
	MIDI              = fromXY(midiButtonX, midiButtonY),
	CV                = fromXY(cvButtonX, cvButtonY),
	KEYBOARD          = fromXY(keyboardButtonX, keyboardButtonY),
	SCALE_MODE        = fromXY(scaleModeButtonX, scaleModeButtonY),
	CROSS_SCREEN_EDIT = fromXY(crossScreenEditButtonX, crossScreenEditButtonY),
	BACK              = fromXY(backButtonX, backButtonY),
	LOAD              = fromXY(loadButtonX, loadButtonY),
	SAVE              = fromXY(saveButtonX, saveButtonY),
	LEARN             = fromXY(learnButtonX, learnButtonY),
	TAP_TEMPO         = fromXY(tapTempoButtonX, tapTempoButtonY),
	SYNC_SCALING      = fromXY(syncScalingButtonX, syncScalingButtonY),
	TRIPLETS          = fromXY(tripletsButtonX, tripletsButtonY),
	PLAY              = fromXY(playButtonX, playButtonY),
	RECORD            = fromXY(recordButtonX, recordButtonY),
	SHIFT             = fromXY(shiftButtonX, shiftButtonY),

	X_ENC             = fromXY(xEncButtonX, xEncButtonY),
	Y_ENC             = fromXY(yEncButtonX, yEncButtonY),
	MOD_ENCODER_0     = fromXY(modEncoder0ButtonX, modEncoder0ButtonY),
	MOD_ENCODER_1     = fromXY(modEncoder1ButtonX, modEncoder1ButtonY),
	SELECT_ENC        = fromXY(selectEncButtonX, selectEncButtonY),
	TEMPO_ENC         = fromXY(tempoEncButtonX, tempoEncButtonY),

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
	MOD_0 = fromXY(0,1),
	MOD_1 = fromXY(0,0),
	MOD_2 = fromXY(1,0),
	MOD_3 = fromXY(1,1),
	MOD_4 = fromXY(2,1),
	MOD_5 = fromXY(3,1),
#else
	MOD_0 = fromXY(1,0),
	MOD_1 = fromXY(1,1),
	MOD_2 = fromXY(1,2),
	MOD_3 = fromXY(1,3),
	MOD_4 = fromXY(2,0),
	MOD_5 = fromXY(2,1),
	MOD_6 = fromXY(2,2),
	MOD_7 = fromXY(2,3),
#endif
};
// clang-format on

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
const Button modButton[6] = {Button::MOD_0, Button::MOD_1, Button::MOD_2, Button::MOD_3, Button::MOD_4, Button::MOD_5};
#else
const Button modButton[8] = {Button::MOD_0, Button::MOD_1, Button::MOD_2, Button::MOD_3,
                             Button::MOD_4, Button::MOD_5, Button::MOD_6, Button::MOD_7};
#endif

constexpr uint8_t fromChar(uint8_t c) {
	return static_cast<uint8_t>(c);
}

struct xy {
	int x, y;
};
struct xy toXY(Button b);
} // namespace button
typedef button::Button Button;

} // namespace hid
