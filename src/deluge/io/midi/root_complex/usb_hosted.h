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

#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/midi_root_complex.h"
#include "util/container/vector/named_thing_vector.h"
#include <array>

class MIDIRootComplexUSBHosted : public MIDIRootComplex {
private:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
	NamedThingVector hostedMIDIDevices_{__builtin_offsetof(MIDICableUSBHosted, name)};
#pragma GCC diagnostic pop

public:
	MIDIRootComplexUSBHosted(const MIDIRootComplexUSBHosted&) = delete;
	MIDIRootComplexUSBHosted(MIDIRootComplexUSBHosted&&) = delete;
	MIDIRootComplexUSBHosted& operator=(const MIDIRootComplexUSBHosted&) = delete;
	MIDIRootComplexUSBHosted& operator=(MIDIRootComplexUSBHosted&&) = delete;

	MIDIRootComplexUSBHosted();
	~MIDIRootComplexUSBHosted() override;

	[[nodiscard]] RootComplexType getType() const override { return RootComplexType::RC_USB_HOST; }

	[[nodiscard]] size_t getNumCables() const override { return hostedMIDIDevices_.getNumElements(); }
	[[nodiscard]] MIDICable* getCable(size_t index) override;

	[[nodiscard]] NamedThingVector& getHostedMIDIDevices() { return hostedMIDIDevices_; }
	[[nodiscard]] NamedThingVector const& getHostedMIDIDevices() const { return hostedMIDIDevices_; }

	void flush() override;
	[[nodiscard]] Error poll() override;
};

extern "C" {
void usbSendCompleteAsHost(int32_t ip);
}
