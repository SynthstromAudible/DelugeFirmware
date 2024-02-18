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

#pragma once

#define encMinBacktrackTime (20 * 44) // In milliseconds/44
#include <cstdint>

namespace deluge::hid::encoders {

class Encoder {
public:
	Encoder();
	void read();
	void setPins(uint8_t pinA1New, uint8_t pinA2New, uint8_t pinB1New, uint8_t pinB2New);
	void setNonDetentMode();
	void interrupt(int32_t which);
	int32_t getLimitedDetentPosAndReset();
	int8_t encPos;    // Keeps track of knob's position relative to centre of closest detent
	int8_t detentPos; // Number of full detents offset since functions last dealt with
private:
	uint8_t portA;
	uint8_t pinA;
	uint8_t portB;
	uint8_t pinB;
	bool pinALastSwitch;
	bool pinBLastSwitch;
	bool pinALastRead;
	bool pinBLastRead;
	int8_t encLastChange; // The "change" applied on the most recently detected action on this knob. Probably 1 or -1.
	bool doDetents;
	bool valuesNow[2];
};

} // namespace deluge::hid::encoders
