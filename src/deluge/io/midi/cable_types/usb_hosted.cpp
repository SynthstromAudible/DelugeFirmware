/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "usb_hosted.h"
#include "storage/storage_manager.h"
#include <string_view>

void MIDICableUSBHosted::writeReferenceAttributesToFile(Serializer& writer) {
	writer.writeAttribute("name", name.get());
	writer.writeAttributeHex("vendorId", vendorId, 4);
	writer.writeAttributeHex("productId", productId, 4);
}

void MIDICableUSBHosted::writeToFlash(uint8_t* memory) {
	*(uint16_t*)memory = vendorId;
	*(uint16_t*)(memory + 2) = productId;
}

std::string_view MIDICableUSBHosted::getDisplayName() {
	return name.get();
}

void MIDICableUSBHosted::callHook(Hook hook) {
	switch (hook) {
	case Hook::HOOK_ON_CONNECTED:
		hookOnConnected();
		break;
	case Hook::HOOK_ON_CHANGE_ROOT_NOTE:
		hookOnChangeRootNote();
		break;
	case Hook::HOOK_ON_CHANGE_SCALE:
		hookOnChangeScale();
		break;
	case Hook::HOOK_ON_ENTER_SCALE_MODE:
		hookOnEnterScaleMode();
		break;
	case Hook::HOOK_ON_EXIT_SCALE_MODE:
		hookOnExitScaleMode();
		break;
	case Hook::HOOK_ON_MIDI_LEARN:
		hookOnMIDILearn();
		break;
	case Hook::HOOK_ON_RECALCULATE_COLOUR:
		hookOnRecalculateColour();
		break;
	case Hook::HOOK_ON_TRANSITION_TO_ARRANGER_VIEW:
		hookOnTransitionToArrangerView();
		break;
	case Hook::HOOK_ON_TRANSITION_TO_CLIP_VIEW:
		hookOnTransitionToClipView();
		break;
	case Hook::HOOK_ON_TRANSITION_TO_SESSION_VIEW:
		hookOnTransitionToSessionView();
		break;
	case Hook::HOOK_ON_WRITE_HOSTED_DEVICE_TO_FILE:
		hookOnWriteHostedDeviceToFile();
		break;
	default:
		break;
	}
}
