#include "CppUTest/TestHarness.h"
#include "dsp/compressor/rms_feedback.h"
#include <fstream>
#include <iostream>

#include "AudioFile.h"

constexpr size_t kTestBufferSize = 1024;

namespace {

TEST_GROUP(RmsFeedback) {
	std::array<StereoSample, kTestBufferSize> testBuffer;

	RMSFeedbackCompressor compressor;

	void setup() {
		testBuffer.fill(StereoSample{0, 0});
		compressor.setup(5 << 24, 5 << 24, 0, 64 << 24, 0);
		compressor.reset();
	}

	void dumpSampleBuffer(char const * path, int bitDepth=24) {
		AudioFile<int32_t> outFile;
		outFile.setAudioBufferSize(2, testBuffer.size());
		outFile.setBitDepth(bitDepth);
		outFile.setSampleRate(44100);
		for (int i = 0; i < testBuffer.size(); ++i) {
			outFile.samples[0][i] = testBuffer[i].l;
			outFile.samples[1][i] = testBuffer[i].r;
		}
		outFile.save(path);
	}
};

// Test compression of the worst-case - 0 threshold and a maximum peak-to-peak value
TEST(RmsFeedback, gainWorstCase) {
	for (auto i = 0; i < testBuffer.size(); i += 2) {
		testBuffer[i + 0].l = (1 << 16) - 1;
		testBuffer[i + 0].r = (1 << 16) - 1;

		testBuffer[i + 1].l = -(1 << 16) + 1;
		testBuffer[i + 1].r = -(1 << 16) + 1;
	}

	dumpSampleBuffer("gainWorstCase_input.wav");

	constexpr size_t windowSize = 16;

	for (int i = 0; i < testBuffer.size(); i += windowSize) {
		compressor.render(&testBuffer.data()[i], windowSize, 1 << 27, 1 << 27, 1 << 30);
	}

	dumpSampleBuffer("gainWorstCase_output.wav");

	for (auto& sample : testBuffer) {
		CHECK(sample.l < (1 << 24));
		CHECK((-(1 << 24)) < sample.l);
		CHECK(sample.r < (1 << 24));
		CHECK((-(1 << 24)) < sample.r);
	}
}

} // namespace
