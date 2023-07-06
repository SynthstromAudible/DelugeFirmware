#pragma once

#include "RZA1/system/r_typedefs.h"
#include "definitions.h"

namespace hid {


namespace button {
	constexpr uint8_t fromXY(int x, int y) {
#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
		return 10 * (y + displayHeight) + x;
#else
		return 9 * (y + displayHeight * 2) + x;
#endif
	}

	typedef uint8_t Button;

	enum KnownButtons : Button {
		affectEntire = fromXY(affectEntireButtonX, affectEntireButtonY),
		sessionView = fromXY(sessionViewButtonX, sessionViewButtonY),
		clipView = fromXY(clipViewButtonX, clipViewButtonY),
		synth = fromXY(synthButtonX, synthButtonY),
		kit = fromXY(kitButtonX, kitButtonY),
		midi = fromXY(midiButtonX, midiButtonY),
		cv = fromXY(cvButtonX, cvButtonY),
		keyboard = fromXY(keyboardButtonX, keyboardButtonY),
		scaleMode = fromXY(scaleModeButtonX, scaleModeButtonY),
		crossScreenEdit = fromXY(crossScreenEditButtonX, crossScreenEditButtonY),
		back = fromXY(backButtonX, backButtonY),
		load = fromXY(loadButtonX, loadButtonY),
		save = fromXY(saveButtonX, saveButtonY),
		learn = fromXY(learnButtonX, learnButtonY),
		tapTempo = fromXY(tapTempoButtonX, tapTempoButtonY),
		syncScaling = fromXY(syncScalingButtonX, syncScalingButtonY),
		triplets = fromXY(tripletsButtonX, tripletsButtonY),
		play = fromXY(playButtonX, playButtonY),
		record = fromXY(recordButtonX, recordButtonY),
		shift = fromXY(shiftButtonX, shiftButtonY),

		xEnc = fromXY(xEncButtonX, xEncButtonY),
		yEnc = fromXY(yEncButtonX, yEncButtonY),
		modEncoder0 = fromXY(modEncoder0ButtonX, modEncoder0ButtonY),
		modEncoder1 = fromXY(modEncoder1ButtonX, modEncoder1ButtonY),
		selectEnc = fromXY(selectEncButtonX, selectEncButtonY),
		tempoEnc = fromXY(tempoEncButtonX, tempoEncButtonY),
	};

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
