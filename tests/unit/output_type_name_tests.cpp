#include "CppUTest/TestHarness.h"
#include "util/functions.h"

TEST_GROUP(OutputTypeNameTests){};

TEST(OutputTypeNameTests, getOutputTypeName_basic_types) {
	// Test basic output types that don't require getCurrentOutput or getDisplayName
	CHECK_EQUAL("Synth", getOutputTypeName(OutputType::SYNTH, 0));
	CHECK_EQUAL("Kit", getOutputTypeName(OutputType::KIT, 0));
	CHECK_EQUAL("CV / gate", getOutputTypeName(OutputType::CV, 0));
	CHECK_EQUAL("None", getOutputTypeName(OutputType::NONE, 0));
}

TEST(OutputTypeNameTests, getOutputTypeName_midi_fallback) {
	// Test MIDI output type - should fallback to "MIDI" when getCurrentOutput returns nullptr
	CHECK_EQUAL("MIDI", getOutputTypeName(OutputType::MIDI_OUT, 1));
}

TEST(OutputTypeNameTests, getOutputTypeName_audio_modes) {
	// Test audio output modes
	CHECK_EQUAL("Audio Player", getOutputTypeName(OutputType::AUDIO, 0));
	CHECK_EQUAL("Audio Sampler", getOutputTypeName(OutputType::AUDIO, 1));
	CHECK_EQUAL("Audio Looper", getOutputTypeName(OutputType::AUDIO, 2));
}
