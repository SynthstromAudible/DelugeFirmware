// The host's MIDI device registry (`io/midi/device_registry.h`) for the shell host specs: the real one compares a cable
// against `MIDIDeviceManager`'s fixed ports, which this tree does not build. Here every cable is a hosted one, given
// the next handle the first time it is named, so a spec can turn an event's device back into the cable it came on.

#include "io/midi/device_registry.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace deluge::midi {
namespace {

std::vector<::MIDICable*> cables; ///< The cables named, in the order they were.

} // namespace

DeviceId id_of(const ::MIDICable& cable) {
	if (const auto at = std::ranges::find(cables, &cable); at != cables.end()) {
		return DeviceId{static_cast<uint32_t>(kFirstHosted.value + (at - cables.begin()))};
	}
	cables.push_back(const_cast<::MIDICable*>(&cable));
	return DeviceId{static_cast<uint32_t>(kFirstHosted.value + cables.size() - 1)};
}

::MIDICable* cable_of(DeviceId id) {
	if (id.value < kFirstHosted.value || id.value - kFirstHosted.value >= cables.size()) {
		return nullptr;
	}
	return cables[id.value - kFirstHosted.value];
}

} // namespace deluge::midi
