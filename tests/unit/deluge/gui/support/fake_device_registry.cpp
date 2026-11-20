// The host's MIDI device registry (`io/midi/device_registry.h`) for the shell host specs: the real one compares a cable
// against `MIDIDeviceManager`'s fixed ports, which this tree does not build. Here every cable is a hosted one, given
// the next handle the first time it is named, so a spec can turn an event's device back into the cable it came on.

#include "io/midi/device_registry.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

namespace deluge::midi {
namespace {

std::array<::MIDICable*, kMaxHostedDevices> cables{}; ///< The cables named, in the order they were.
uint16_t count = 0;                                   ///< How many are named.

} // namespace

DeviceId id_of(const ::MIDICable& cable) {
	const auto seen = std::span{cables}.first(count);
	if (const auto at = std::ranges::find(seen, &cable); at != seen.end()) {
		return DeviceId{static_cast<uint16_t>(kFirstHosted.value + (at - seen.begin()))};
	}
	if (count == kMaxHostedDevices) {
		return {};
	}
	cables[count] = const_cast<::MIDICable*>(&cable);
	return DeviceId{static_cast<uint16_t>(kFirstHosted.value + count++)};
}

::MIDICable* cable_of(DeviceId id) {
	if (id.value < kFirstHosted.value || id.value - kFirstHosted.value >= count) {
		return nullptr;
	}
	return cables[id.value - kFirstHosted.value];
}

} // namespace deluge::midi
