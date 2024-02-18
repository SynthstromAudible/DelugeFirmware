/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "hid/encoder.h"
#include "processing/engines/audio_engine.h"

using deluge::hid::encoders::Encoder;

extern "C" {
#include "RZA1/gpio/gpio.h"
}

Encoder::Encoder() {
	encPos = 0;
	detentPos = 0;
	encLastChange = 0;
	pinALastSwitch = 1;
	pinBLastSwitch = 1;
	pinALastRead = 1;
	pinBLastRead = 1;
	doDetents = true;
	valuesNow[0] = true;
	valuesNow[1] = true;
}

void Encoder::read() {
	bool pinANewVal = readInput(portA, pinA);
	bool pinBNewVal = readInput(portB, pinB);

	// If they've both changed...
	if (pinANewVal != pinALastSwitch && pinBNewVal != pinBLastSwitch) {

		int32_t change = 0;

		// Had pin A changed first?
		if (pinALastRead != pinALastSwitch) {
			change = (pinALastSwitch == pinBLastSwitch) ? -1 : 1;
			pinALastSwitch = pinANewVal;
		}

		// Or had pin B changed first?
		else if (pinBLastRead != pinBLastSwitch) {
			change = (pinALastSwitch == pinBLastSwitch) ? 1 : -1;
			pinBLastSwitch = pinBNewVal;
		}

		// Or if they both changed at the same time
		else {

			// If detents, we have to do a thing to ensure we don't end up "in between" detents
			if (doDetents) {
				change = (encLastChange >= 0) ? 2 : -2;
			}
			pinALastSwitch = pinANewVal;
			pinBLastSwitch = pinBNewVal;
		}

		if (change != 0) {

			encPos += change;

			if (doDetents) {
				while (encPos > 2) {
					encPos -= 4;
					detentPos += 1;
				}

				while (encPos < -2) {
					encPos += 4;
					detentPos -= 1;
				}
			}
			encLastChange = change;
		}
	}

	pinALastRead = pinANewVal;
	pinBLastRead = pinBNewVal;
}

void Encoder::interrupt(int32_t which) {

	valuesNow[which] = !valuesNow[which];

	// If they've both changed...
	if (valuesNow[0] != pinALastSwitch && valuesNow[1] != pinBLastSwitch) {

		// Had pin A changed first?
		if (pinALastRead != pinALastSwitch) {
			encPos += (pinALastSwitch == pinBLastSwitch) ? -1 : 1;
			pinALastSwitch = valuesNow[0];
		}

		// Or had pin B changed first?
		else {
			encPos += (pinALastSwitch == pinBLastSwitch) ? 1 : -1;
			pinBLastSwitch = valuesNow[1];
		}
	}

	pinALastRead = valuesNow[0];
}

void Encoder::setPins(uint8_t pinA1New, uint8_t pinA2New, uint8_t pinB1New, uint8_t pinB2New) {
	portA = pinA1New;
	pinA = pinA2New;
	portB = pinB1New;
	pinB = pinB2New;
	setPinAsInput(portA, pinA);
	setPinAsInput(portB, pinB);
}

void Encoder::setNonDetentMode() {
	doDetents = false;
	pinALastSwitch = readInput(portA, pinA);
	pinBLastSwitch = readInput(portB, pinB);
}

int32_t Encoder::getLimitedDetentPosAndReset() {
	int32_t toReturn = (detentPos >= 0) ? 1 : -1;
	detentPos = 0;
	return toReturn;
}
