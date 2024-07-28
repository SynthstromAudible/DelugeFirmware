/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#include "gui/menu_item/toggle.h"
#include "model/instrument/midi_instrument.h"

extern Output* getCurrentOutput();
extern Instrument* getCurrentInstrument();

namespace deluge::gui::menu_item::midi {
class MPEYToModWheel final : public Toggle {
public:
	using Toggle::Toggle;
	// this is safe since it's only shown in midi clips
	void readCurrentValue() override {
		// awkward but this avoids needing to branch every time we output a cc
		this->setValue(((MIDIInstrument*)getCurrentOutput())->outputMPEY == CC_EXTERNAL_MOD_WHEEL);
	}
	void writeCurrentValue() override {
		((MIDIInstrument*)getCurrentOutput())->outputMPEY =
		    this->getValue() ? CC_EXTERNAL_MOD_WHEEL : CC_EXTERNAL_MPE_Y;
		getCurrentInstrument()->editedByUser = true;
	}
};
} // namespace deluge::gui::menu_item::midi
