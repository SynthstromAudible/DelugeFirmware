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

namespace {

uint8_t storedOutputDeviceIndex() {
	if (SoundEditor::editingCVOrMIDIClip()) {
		auto* instrument = getCurrentInstrument();
		if (instrument != nullptr && instrument->type == OutputType::MIDI_OUT) {
			return static_cast<MIDIInstrument*>(instrument)->outputDevice;
		}
	}
	else if (soundEditor.editingKitRow()) {
		auto* kit = getCurrentKit();
		if (kit != nullptr && kit->selectedDrum != nullptr && kit->selectedDrum->type == DrumType::MIDI) {
			return static_cast<MIDIDrum*>(kit->selectedDrum)->outputDevice;
		}
	}
	return 0;
}

bool currentMidiTrackSendsToMPE() {
	if (SoundEditor::editingCVOrMIDIClip()) {
		auto* instrument = getCurrentInstrument();
		if (instrument != nullptr && instrument->type == OutputType::MIDI_OUT) {
			return static_cast<MIDIInstrument*>(instrument)->sendsToMPE();
		}
	}
	return false;
}

} // namespace

void OutputDeviceSelection::beginSession(MenuItem* navigated_backward_from) {
	Selection::beginSession(navigated_backward_from);
	readCurrentValue();
}

void OutputDeviceSelection::readCurrentValue() {
	uint8_t stored = storedOutputDeviceIndex();
	bool include_mpe = currentMidiTrackSendsToMPE();
	this->setValue(deluge::io::midi::deviceIndexToMenuSlot(stored, stored, include_mpe));
}

void OutputDeviceSelection::writeCurrentValue() {
	uint8_t stored = storedOutputDeviceIndex();
	bool include_mpe = currentMidiTrackSendsToMPE();
	uint8_t current_device =
	    deluge::io::midi::menuSlotToDeviceIndex(static_cast<uint8_t>(this->getValue()), stored, include_mpe);
	auto device_name = deluge::io::midi::getDeviceNameForIndex(current_device);

	if (SoundEditor::editingCVOrMIDIClip()) {
		auto* instrument = getCurrentInstrument();
		if (instrument != nullptr && instrument->type == OutputType::MIDI_OUT) {
			auto* midi_instrument = static_cast<MIDIInstrument*>(instrument);
			midi_instrument->outputDevice = current_device;
			if (!device_name.empty()) {
				midi_instrument->outputDeviceName.set(device_name.data());
			}
		}
	}
	else if (soundEditor.editingKitRow()) {
		auto* kit = getCurrentKit();
		if (kit != nullptr && kit->selectedDrum != nullptr && kit->selectedDrum->type == DrumType::MIDI) {
			auto* midi_drum = static_cast<MIDIDrum*>(kit->selectedDrum);
			midi_drum->outputDevice = current_device;
			if (!device_name.empty()) {
				midi_drum->outputDeviceName.set(device_name.data());
			}
		}
	}
}

deluge::vector<std::string_view>
OutputDeviceSelection::getOptions(OptType opt_type) { // NOLINT(readability-convert-member-functions-to-static)
	(void)opt_type;
	uint8_t stored = storedOutputDeviceIndex();
	bool include_mpe = currentMidiTrackSendsToMPE();
	return deluge::io::midi::getAllMIDIDeviceNames(stored, include_mpe);
}

} // namespace deluge::gui::menu_item::midi
