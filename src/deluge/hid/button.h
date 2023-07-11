#pragma once

#include "RZA1/system/r_typedefs.h"
#include "definitions.h"

namespace hid {

namespace button {
constexpr uint8_t fromXY(int x, int y) {
	return 9 * (y + displayHeight * 2) + x;
}

typedef uint8_t Button;

// clang-format off
enum KnownButtons : Button {
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
};
// clang-format on

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
