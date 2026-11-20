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
#include "io/midi/cable_types/usb_hosted.h"
#include "io/midi/midi_device_manager.h"
#include "util/containers.h"
#include "util/exceptions.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace deluge::midi {
namespace {

// More handles than a 32-bit address space holds hosted cables, so `id_of` never runs out of them on the device.
static_assert(sizeof(::MIDICableUSBHosted) > 1);
static_assert(std::numeric_limits<decltype(DeviceId::value)>::max() - kFirstHosted.value
              >= std::numeric_limits<uint32_t>::max() / 2);

/// The hosted cables named, in the order their handles were given.
deluge::fast_vector<::MIDICable*>& hosted() {
	static deluge::fast_vector<::MIDICable*> table;
	return table;
}

[[nodiscard]] DeviceId hosted_id(size_t index) {
	return DeviceId{static_cast<uint32_t>(kFirstHosted.value + index)};
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
	deluge::fast_vector<::MIDICable*>& table = hosted();
	if (const auto at = std::ranges::find(table, &cable); at != table.end()) {
		return hosted_id(at - table.begin());
	}
	try {
		// The one place a `const MIDICable&` (the scenes' learn paths hold it const) becomes the mutable pointer the
		// legacy writes `cable_of` serves need. Every cable is a mutable object, so the cast is sound.
		table.push_back(const_cast<::MIDICable*>(&cable));
	} catch (deluge::exception&) {
		return {};
	}
	return hosted_id(table.size() - 1);
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
	const deluge::fast_vector<::MIDICable*>& table = hosted();
	if (id.value < kFirstHosted.value || id.value - kFirstHosted.value >= table.size()) {
		return nullptr;
	}
	return table[id.value - kFirstHosted.value];
}

} // namespace deluge::midi
