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

#include "io/midi/device_registry.h"

#include "io/midi/cable_types/din.h"
#include "io/midi/cable_types/usb_device_cable.h"
#include "io/midi/midi_device_manager.h"

#include <algorithm>
#include <array>
#include <span>

namespace deluge::midi {
namespace {

/// The hosted cables given a handle, in the order they were given one: a cable's index here is its handle's offset
/// from `kFirstHosted`.
struct Hosted {
	std::array<::MIDICable*, kMaxHostedDevices> cables{}; ///< The first `count` entries are the cables named.
	uint16_t count{0};                                    ///< How many handles have been given.
};

Hosted& hosted() {
	static Hosted table;
	return table;
}

[[nodiscard]] DeviceId hosted_id(size_t index) {
	return DeviceId{static_cast<uint16_t>(kFirstHosted.value + index)};
}

} // namespace

DeviceId id_of(const ::MIDICable& cable) {
	if (&cable == &MIDIDeviceManager::dinMIDIPorts) {
		return kDin;
	}
	if (&cable == &MIDIDeviceManager::upstreamUSBMIDICable1) {
		return kUpstreamUsb1;
	}
	if (&cable == &MIDIDeviceManager::upstreamUSBMIDICable2) {
		return kUpstreamUsb2;
	}
	if (&cable == &MIDIDeviceManager::upstreamUSBMIDICable3) {
		return kUpstreamUsb3;
	}
	Hosted& table = hosted();
	const auto seen = std::span{table.cables}.first(table.count);
	if (const auto at = std::ranges::find(seen, &cable); at != seen.end()) {
		return hosted_id(at - seen.begin());
	}
	if (table.count == kMaxHostedDevices) {
		return {};
	}
	// The one place a `const MIDICable&` (the scenes' learn paths hold it const) becomes the mutable pointer the
	// legacy writes `cable_of` serves need. Every cable is a mutable object, so the cast is sound.
	table.cables[table.count] = const_cast<::MIDICable*>(&cable);
	return hosted_id(table.count++);
}

::MIDICable* cable_of(DeviceId id) {
	if (id == kDin) {
		return &MIDIDeviceManager::dinMIDIPorts;
	}
	if (id == kUpstreamUsb1) {
		return &MIDIDeviceManager::upstreamUSBMIDICable1;
	}
	if (id == kUpstreamUsb2) {
		return &MIDIDeviceManager::upstreamUSBMIDICable2;
	}
	if (id == kUpstreamUsb3) {
		return &MIDIDeviceManager::upstreamUSBMIDICable3;
	}
	const Hosted& table = hosted();
	if (id.value < kFirstHosted.value || id.value - kFirstHosted.value >= table.count) {
		return nullptr;
	}
	return table.cables[id.value - kFirstHosted.value];
}

} // namespace deluge::midi
