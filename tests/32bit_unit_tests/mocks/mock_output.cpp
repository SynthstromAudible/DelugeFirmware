#include "model/output.h"
#include "model/song/song.h"

// Mock implementation of getCurrentOutput for unit tests
// This provides a minimal stub that returns nullptr to avoid linking errors
Output* getCurrentOutput() {
	return nullptr;
}

// Mock implementation of getCurrentClip for unit tests
Clip* getCurrentClip() {
	return nullptr;
}

// Mock implementation of getCurrentInstrumentClip for unit tests
InstrumentClip* getCurrentInstrumentClip() {
	return nullptr;
}

// Mock implementation of getCurrentAudioClip for unit tests
AudioClip* getCurrentAudioClip() {
	return nullptr;
}

// Mock implementation of getCurrentKit for unit tests
Kit* getCurrentKit() {
	return nullptr;
}

// Mock implementation of getCurrentInstrument for unit tests
Instrument* getCurrentInstrument() {
	return nullptr;
}

// Mock implementation of getCurrentOutputType for unit tests
OutputType getCurrentOutputType() {
	return OutputType::NONE;
}
