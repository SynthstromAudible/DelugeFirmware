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

#pragma once

#include "deluge/io/midi/cable_types/usb_common.h"
#include <string_view>

class MIDICableUSBHosted : public MIDICableUSB {
public:
	virtual ~MIDICableUSBHosted() = default;
	MIDICableUSBHosted() = default;

	void writeReferenceAttributesToFile(Serializer& writer) override;
	void writeToFlash(uint8_t* memory) override;
	std::string_view getDisplayName() override;

	/// @name Hooks
	/// @{

	// Add new hooks to the below list.

	// Gets called once for each freshly connected hosted device.
	virtual void hookOnConnected(){};

	// Gets called when something happens that changes the root note
	virtual void hookOnChangeRootNote(){};

	// Gets called when something happens that changes the scale/mode
	virtual void hookOnChangeScale(){};

	// Gets called when entering Scale Mode in a clip
	virtual void hookOnEnterScaleMode(){};

	// Gets called when exiting Scale Mode in a clip
	virtual void hookOnExitScaleMode(){};

	// Gets called when learning/unlearning a midi device to a clip
	virtual void hookOnMIDILearn(){};

	// Gets called when recalculating colour in clip mode
	virtual void hookOnRecalculateColour(){};

	// Gets called when transitioning to ArrangerView
	virtual void hookOnTransitionToArrangerView(){};

	// Gets called when transitioning to ClipView
	virtual void hookOnTransitionToClipView(){};

	// Gets called when transitioning to SessionView
	virtual void hookOnTransitionToSessionView(){};

	// Gets called when hosted device info is saved to XML (usually after changing settings)
	virtual void hookOnWriteHostedDeviceToFile(){};

	// Add an entry to this enum if adding new virtual hook functions above
	enum class Hook {
		HOOK_ON_CONNECTED = 0,
		HOOK_ON_CHANGE_ROOT_NOTE,
		HOOK_ON_CHANGE_SCALE,
		HOOK_ON_ENTER_SCALE_MODE,
		HOOK_ON_EXIT_SCALE_MODE,
		HOOK_ON_MIDI_LEARN,
		HOOK_ON_RECALCULATE_COLOUR,
		HOOK_ON_TRANSITION_TO_ARRANGER_VIEW,
		HOOK_ON_TRANSITION_TO_CLIP_VIEW,
		HOOK_ON_TRANSITION_TO_SESSION_VIEW,
		HOOK_ON_WRITE_HOSTED_DEVICE_TO_FILE
	};

	/// @}

	// Ensure to add a case to this function in midi_device.cpp when adding new hooks
	void callHook(Hook hook);

	uint16_t vendorId{};
	uint16_t productId{};

	bool freshly_connected = true; // Used to trigger hookOnConnected from the input loop

	String name;
};
