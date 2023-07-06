#pragma once

#include "RZA1/system/r_typedefs.h"
#include "definitions.h"

namespace hid {

class Button {
public:
	int x, y;

	Button(uint8_t value);
	Button(int x, int y);
	uint8_t toChar();
	bool isButton();
	static bool isButton(uint8_t value);
	bool operator==(Button const& other);
};

namespace button {
const Button affectEntire(affectEntireButtonX, affectEntireButtonY);
const Button sessionView(sessionViewButtonX, sessionViewButtonY);
const Button clipView(clipViewButtonX, clipViewButtonY);
const Button synth(synthButtonX, synthButtonY);
const Button kit(kitButtonX, kitButtonY);
const Button midi(midiButtonX, midiButtonY);
const Button cv(cvButtonX, cvButtonY);
const Button keyboard(keyboardButtonX, keyboardButtonY);
const Button scaleMode(scaleModeButtonX, scaleModeButtonY);
const Button crossScreenEdit(crossScreenEditButtonX, crossScreenEditButtonY);
const Button back(backButtonX, backButtonY);
const Button load(loadButtonX, loadButtonY);
const Button save(saveButtonX, saveButtonY);
const Button learn(learnButtonX, learnButtonY);
const Button tapTempo(tapTempoButtonX, tapTempoButtonY);
const Button syncScaling(syncScalingButtonX, syncScalingButtonY);
const Button triplets(tripletsButtonX, tripletsButtonY);
const Button play(playButtonX, playButtonY);
const Button record(recordButtonX, recordButtonY);
const Button shift(shiftButtonX, shiftButtonY);

const Button xEnc(xEncButtonX, xEncButtonY);
const Button yEnc(yEncButtonX, yEncButtonY);
const Button modEncoder0(modEncoder0ButtonX, modEncoder0ButtonY);
const Button modEncoder1(modEncoder1ButtonX, modEncoder1ButtonY);
const Button selectEnc(selectEncButtonX, selectEncButtonY);
const Button tempoEnc(tempoEncButtonX, tempoEncButtonY);
} // namespace button

} // namespace hid
