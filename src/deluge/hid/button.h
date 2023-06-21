#pragma once

#include "definitions_cxx.hpp"
#include <cstdint>

namespace deluge::hid {

namespace button {
constexpr uint8_t fromCartesian(Cartesian c) {
	return 9 * (c.y + kDisplayHeight * 2) + c.x;
}

constexpr uint8_t fromXY(int32_t x, int32_t y) {
	return 9 * (y + kDisplayHeight * 2) + x;
}

using Button = uint8_t;

// TODO: these are duplicate
static constexpr uint8_t ZmodButtonX[8] = {1, 1, 1, 1, 2, 2, 2, 2};
static constexpr uint8_t ZmodButtonY[8] = {0, 1, 2, 3, 0, 1, 2, 3};

// clang-format off
enum KnownButtons : Button {
	AFFECT_ENTIRE     = fromCartesian(affectEntireButtonCoord),
	SESSION_VIEW      = fromCartesian(sessionViewButtonCoord),
	CLIP_VIEW         = fromCartesian(clipViewButtonCoord),
	SYNTH             = fromCartesian(synthButtonCoord),
	KIT               = fromCartesian(kitButtonCoord),
	MIDI              = fromCartesian(midiButtonCoord),
	CV                = fromCartesian(cvButtonCoord),
	KEYBOARD          = fromCartesian(keyboardButtonCoord),
	SCALE_MODE        = fromCartesian(scaleModeButtonCoord),
	CROSS_SCREEN_EDIT = fromCartesian(crossScreenEditButtonCoord),
	BACK              = fromCartesian(backButtonCoord),
	LOAD              = fromCartesian(loadButtonCoord),
	SAVE              = fromCartesian(saveButtonCoord),
	LEARN             = fromCartesian(learnButtonCoord),
	TAP_TEMPO         = fromCartesian(tapTempoButtonCoord),
	SYNC_SCALING      = fromCartesian(syncScalingButtonCoord),
	TRIPLETS          = fromCartesian(tripletsButtonCoord),
	PLAY              = fromCartesian(playButtonCoord),
	RECORD            = fromCartesian(recordButtonCoord),
	SHIFT             = fromCartesian(shiftButtonCoord),

	MOD7              = fromXY(ZmodButtonX[6], ZmodButtonY[6]),

	X_ENC             = fromCartesian(xEncButtonCoord),
	Y_ENC             = fromCartesian(yEncButtonCoord),
	MOD_ENCODER_0     = fromCartesian(modEncoder0ButtonCoord),
	MOD_ENCODER_1     = fromCartesian(modEncoder1ButtonCoord),
	SELECT_ENC        = fromCartesian(selectEncButtonCoord),
	TEMPO_ENC         = fromCartesian(tempoEncButtonCoord),
};
// clang-format on

constexpr uint8_t fromChar(uint8_t c) {
	return static_cast<uint8_t>(c);
}

Cartesian toXY(Button b);
} // namespace button
using Button = button::Button;

} // namespace deluge::hid
