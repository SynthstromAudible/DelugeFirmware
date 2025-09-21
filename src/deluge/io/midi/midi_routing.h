#pragma once

#include <cstdint>

namespace deluge::io::midi {

/**
 * MIDI routing configuration for output device and channel selection
 *
 * This data class replaces the previous bitmask approach for MIDI output
 * device selection, providing a more structured and extensible way to
 * manage MIDI routing.
 */
struct MIDIRouting {
	// 0 = All devices, 1 = DIN, 2+ = USB devices (device index + 1)
	uint8_t device{0};
	// MIDI channel (0-15)
	uint8_t channel{0};

	MIDIRouting() = default;

	// Constructor with device and channel
	MIDIRouting(uint8_t device, uint8_t channel) : device(device), channel(channel) {}

	// Convert to device filter bitmask for MIDI engine compatibility
	uint32_t toDeviceFilter() const {
		if (device == 0) {
			return 0; // All devices
		}
		if (device == 1) {
			return 1; // DIN only (bit 0)
		}
		return (1 << (device - 1)); // USB device (bit device-1)
	}

	// Check if routing is valid
	bool isValid() const {
		return channel < 16; // Valid MIDI channels are 0-15
	}

	// Equality operator
	bool operator==(const MIDIRouting& other) const { return device == other.device && channel == other.channel; }

	// Inequality operator
	bool operator!=(const MIDIRouting& other) const { return !(*this == other); }
};

} // namespace deluge::io::midi
