/*
 * Copyright © 2024 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 */

#include "gui/menu_item/midi/output_device_selection.h"
#include "gui/ui/sound_editor.h"
#include "io/midi/midi_device_helper.h"
#include "model/drum/midi_drum.h"
#include "model/instrument/kit.h"
#include "model/instrument/midi_instrument.h"
#include "model/song/song.h"

namespace deluge::gui::menu_item::midi {

void OutputDeviceSelection::beginSession(MenuItem* navigatedBackwardFrom) {
	Selection::beginSession(navigatedBackwardFrom);
	readCurrentValue();
}

void OutputDeviceSelection::readCurrentValue() {
	uint8_t value = 0;
	if (soundEditor.editingCVOrMIDIClip()) {
		auto* instrument = ::getCurrentInstrument();
		if (instrument != nullptr && instrument->type == OutputType::MIDI_OUT) {
			value = static_cast<MIDIInstrument*>(instrument)->outputDevice;
		}
	}
	else if (soundEditor.editingKitRow()) {
		auto* kit = ::getCurrentKit();
		if (kit != nullptr && kit->selectedDrum != nullptr && kit->selectedDrum->type == DrumType::MIDI) {
			value = static_cast<MIDIDrum*>(kit->selectedDrum)->outputDevice;
		}
	}

	auto options = deluge::io::midi::getAllMIDIDeviceNames();
	if (value >= options.size()) {
		value = 0;
	}
	this->setValue(value);
}

void OutputDeviceSelection::writeCurrentValue() {
	uint8_t currentDevice = static_cast<uint8_t>(this->getValue());
	auto deviceName = deluge::io::midi::getDeviceNameForIndex(currentDevice);

	if (soundEditor.editingCVOrMIDIClip()) {
		auto* instrument = ::getCurrentInstrument();
		if (instrument != nullptr && instrument->type == OutputType::MIDI_OUT) {
			auto* midiInstrument = static_cast<MIDIInstrument*>(instrument);
			midiInstrument->outputDevice = currentDevice;
			if (!deviceName.empty()) {
				midiInstrument->outputDeviceName.set(deviceName.data());
			}
		}
	}
	else if (soundEditor.editingKitRow()) {
		auto* kit = ::getCurrentKit();
		if (kit != nullptr && kit->selectedDrum != nullptr && kit->selectedDrum->type == DrumType::MIDI) {
			auto* midiDrum = static_cast<MIDIDrum*>(kit->selectedDrum);
			midiDrum->outputDevice = currentDevice;
			if (!deviceName.empty()) {
				midiDrum->outputDeviceName.set(deviceName.data());
			}
		}
	}
}

deluge::vector<std::string_view> OutputDeviceSelection::getOptions(OptType optType) {
	(void)optType;
	return deluge::io::midi::getAllMIDIDeviceNames();
}

} // namespace deluge::gui::menu_item::midi
