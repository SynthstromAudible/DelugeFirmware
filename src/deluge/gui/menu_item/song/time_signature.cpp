/*
 * Copyright © 2025 Synthstrom Audible Limited
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

#include "gui/menu_item/song/time_signature.h"
#include "model/song/song.h"

extern Song* currentSong;

namespace deluge::gui::menu_item::song {

// Numerator (1-32)
void TimeSignatureNumerator::readCurrentValue() {
	this->setValue(currentSong->defaultTimeSignature.numerator);
}

void TimeSignatureNumerator::writeCurrentValue() {
	currentSong->defaultTimeSignature.numerator = static_cast<uint8_t>(this->getValue());
}

// Denominator (2, 4, 8, 16)
static constexpr uint8_t kDenominatorValues[] = {2, 4, 8, 16};

void TimeSignatureDenominator::readCurrentValue() {
	for (int i = 0; i < 4; i++) {
		if (kDenominatorValues[i] == currentSong->defaultTimeSignature.denominator) {
			this->setValue(i);
			return;
		}
	}
	this->setValue(1); // default to quarter
}

void TimeSignatureDenominator::writeCurrentValue() {
	currentSong->defaultTimeSignature.denominator = kDenominatorValues[this->getValue()];
}

deluge::vector<std::string_view> TimeSignatureDenominator::getOptions(OptType optType) {
	return {"Half", "Quarter", "Eighth", "Sixteenth"};
}

} // namespace deluge::gui::menu_item::song
