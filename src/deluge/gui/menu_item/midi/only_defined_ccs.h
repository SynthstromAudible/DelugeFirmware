/*
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
#include "model/song/song.h"

namespace deluge::gui::menu_item::midi {

// When enabled, scrolling through MIDI CCs in automation view skips any CC that has no defined (named)
// label, so only the CCs described by the loaded device definition are reachable. Persisted per-device
// in the device definition file (see MIDIInstrument::writeDeviceDefinitionFile).
class OnlyDefinedCCs final : public Toggle {
public:
	using Toggle::Toggle;
	// this is safe since it's only shown in midi clips
	void readCurrentValue() override {
		this->setValue(static_cast<MIDIInstrument*>(getCurrentOutput())->only_show_defined_ccs);
	}
	void writeCurrentValue() override {
		static_cast<MIDIInstrument*>(getCurrentOutput())->only_show_defined_ccs = this->getValue();
		getCurrentInstrument()->editedByUser = true;
	}
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		// not relevant for cv
		return (getCurrentOutputType() == OutputType::MIDI_OUT);
	}
};

} // namespace deluge::gui::menu_item::midi
