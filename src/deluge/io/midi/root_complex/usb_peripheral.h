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

#include "io/midi/cable_types/usb_device_cable.h"
#include "io/midi/midi_root_complex.h"
#include <array>

class MIDIRootComplexUSBPeripheral : public MIDIRootComplex {
private:
	using CableArray = std::array<MIDICableUSBUpstream, 3>;

	CableArray cables_;

public:
	MIDIRootComplexUSBPeripheral(const MIDIRootComplexUSBPeripheral&) = delete;
	MIDIRootComplexUSBPeripheral(MIDIRootComplexUSBPeripheral&&) = delete;
	MIDIRootComplexUSBPeripheral& operator=(const MIDIRootComplexUSBPeripheral&) = delete;
	MIDIRootComplexUSBPeripheral& operator=(MIDIRootComplexUSBPeripheral&&) = delete;

	MIDIRootComplexUSBPeripheral();
	~MIDIRootComplexUSBPeripheral() override;

	[[nodiscard]] size_t getNumCables() const override {
		// This returns 2, not 3, because the 3rd cable is secret (only used by sysex infrastructure)
		return 2;
	}

	[[nodiscard]] MIDICable* getCable(size_t index) override;
	[[nodiscard]] RootComplexType getType() const override { return RootComplexType::RC_USB_PERIPHERAL; }

	void flush() override;
	[[nodiscard]] Error poll() override;
};
