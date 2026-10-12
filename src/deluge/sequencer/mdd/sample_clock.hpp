#pragma once
#include <cstdint>
namespace snake {
// Extend Deluge's wrapping sample counter before converting to milliseconds.
// Calls during a gesture must be less than one complete sample-counter cycle apart.
struct SampleClock {
	uint32_t previous = 0;
	uint64_t samples = 0;
	uint64_t milliseconds(uint32_t current) {
		samples += uint32_t(current - previous);
		previous = current;
		return samples * 1000 / 44100;
	}
};
} // namespace snake
