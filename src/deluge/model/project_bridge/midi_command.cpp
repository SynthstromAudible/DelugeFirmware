/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "model/project_bridge/midi_command.h"

#include "io/midi/cable_types/din.h"
#include "io/midi/cable_types/usb_device_cable.h"
#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/midi_device_manager.h"
#include "storage/storage_manager.h"

#include <strings.h>

namespace deluge::bridge {

using deluge::project::MidiCommand;
using deluge::project::MidiDeviceRef;

std::optional<MidiDeviceRef> reference_of(MIDICable& cable) {
	using Port = MidiDeviceRef::Port;
	if (&cable == &MIDIDeviceManager::dinMIDIPorts) {
		return MidiDeviceRef{.port = Port::DIN};
	}
	if (&cable == &MIDIDeviceManager::upstreamUSBMIDICable1) {
		return MidiDeviceRef{.port = Port::UPSTREAM_USB_1};
	}
	if (&cable == &MIDIDeviceManager::upstreamUSBMIDICable2) {
		return MidiDeviceRef{.port = Port::UPSTREAM_USB_2};
	}
	if (&cable == &MIDIDeviceManager::upstreamUSBMIDICable3) {
		return MidiDeviceRef{.port = Port::UPSTREAM_USB_3};
	}
	MIDICableUSBHosted* hosted = cable.asHosted();
	if (hosted == nullptr) {
		return std::nullopt;
	}
	return MidiDeviceRef{
	    .port = Port::HOSTED, .name = hosted->name, .vendor_id = hosted->vendorId, .product_id = hosted->productId};
}

MIDICable* find_cable(const MidiDeviceRef& device) {
	using Port = MidiDeviceRef::Port;
	switch (device.port) {
	case Port::DIN:
		return &MIDIDeviceManager::dinMIDIPorts;
	case Port::UPSTREAM_USB_1:
		return &MIDIDeviceManager::upstreamUSBMIDICable1;
	case Port::UPSTREAM_USB_2:
		return &MIDIDeviceManager::upstreamUSBMIDICable2;
	case Port::UPSTREAM_USB_3:
		return &MIDIDeviceManager::upstreamUSBMIDICable3;
	case Port::HOSTED:
		break;
	}
	// The order the device manager resolves a reference in: by name first, then by USB ids.
	if (!device.name.empty()) {
		for (MIDICableUSBHosted* candidate : MIDIDeviceManager::hostedMIDIDevices) {
			if (strcasecmp(candidate->name.c_str(), device.name.c_str()) == 0) {
				return candidate;
			}
		}
	}
	for (MIDICableUSBHosted* candidate : MIDIDeviceManager::hostedMIDIDevices) {
		if (candidate->vendorId == device.vendor_id && candidate->productId == device.product_id) {
			return candidate;
		}
	}
	return nullptr;
}

LearnedMIDI learned_of(const MidiCommand& command) {
	LearnedMIDI learned;
	learned.channelOrZone = command.channel_or_zone;
	learned.noteOrCC = command.note_or_cc;
	learned.cable = command.device ? find_cable(*command.device) : nullptr;
	return learned;
}

MidiCommand command_of(const LearnedMIDI& learned) {
	MidiCommand command{.channel_or_zone = learned.channelOrZone, .note_or_cc = learned.noteOrCC};
	if (learned.cable != nullptr) {
		command.device = reference_of(*learned.cable);
	}
	return command;
}

bool matches(const MidiCommand& command, MIDICable& from, int32_t channel, int32_t noteOrCC) {
	if (noteOrCC != command.note_or_cc || channel != command.channel_or_zone) {
		return false;
	}
	if (!MIDIDeviceManager::differentiatingInputsByDevice || !command.device) {
		return true;
	}
	return find_cable(*command.device) == &from;
}

void write_reference(Serializer& writer, char const* tagName, const MidiDeviceRef& device) {
	using Port = MidiDeviceRef::Port;
	writer.writeOpeningTagBeginning(tagName);
	switch (device.port) {
	case Port::DIN:
		writer.writeAttribute("port", "din", false);
		break;
	case Port::UPSTREAM_USB_1:
		writer.writeAttribute("port", "upstreamUSB", false);
		break;
	case Port::UPSTREAM_USB_2:
	case Port::UPSTREAM_USB_3: // the upstream cables write every port after the first as "upstreamUSB2"
		writer.writeAttribute("port", "upstreamUSB2", false);
		break;
	case Port::HOSTED:
		writer.writeAttribute("name", device.name.c_str());
		writer.writeAttributeHex("vendorId", device.vendor_id, 4);
		writer.writeAttributeHex("productId", device.product_id, 4);
		break;
	}
	writer.closeTag();
}

void write_command(Serializer& writer, char const* tagName, const MidiCommand& command, int32_t midiMessageType) {
	if (!command.contains_something()) {
		return;
	}
	LearnedMIDI attributes =
	    learned_of(MidiCommand{.channel_or_zone = command.channel_or_zone, .note_or_cc = command.note_or_cc});
	writer.writeOpeningTagBeginning(tagName);
	attributes.writeAttributesToFile(writer, midiMessageType);
	if (command.device) {
		writer.writeOpeningTagEnd();
		write_reference(writer, "device", *command.device);
		writer.writeClosingTag(tagName);
	}
	else {
		writer.closeTag();
	}
}

} // namespace deluge::bridge
