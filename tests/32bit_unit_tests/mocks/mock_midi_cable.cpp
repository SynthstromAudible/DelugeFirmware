#include "io/midi/midi_device.h"
#include <string>

// Mock implementation of MIDICable for unit tests
class MockMIDICable : public MIDICable {
public:
	MockMIDICable() = default;

	// Mock implementation of getDisplayName
	char const* getDisplayName() const override { return "Mock USB Device"; }

	// Mock implementation of writeToFlash
	void writeToFlash(uint8_t* memory) override {
		// No-op for tests
	}

	// Mock implementation of sendMessage
	Error sendMessage(MIDIMessage message) override { return Error::NONE; }

	// Mock implementation of sendSysex
	Error sendSysex(const uint8_t* data, int32_t len) override { return Error::NONE; }

	// Mock implementation of sendBufferSpace
	size_t sendBufferSpace() const override {
		return 1024; // Return a reasonable buffer size
	}
};

// Global mock instance for tests
MockMIDICable mockMIDICable;
