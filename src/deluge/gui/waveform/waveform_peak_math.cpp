#include "gui/waveform/waveform_peak_math.h"

#include <algorithm>
#include <limits>

WaveformPeak scanClusterPeak(const char* clusterData, int32_t startByte, int32_t endByte, int32_t byteDepth,
                             int32_t numChannels) {
	int32_t numSamplesToRead = (endByte - startByte) / byteDepth;
	int32_t byteIncrement = byteDepth;

	// We don't want to read endless samples. If we were gonna read lots, skip some.
	int32_t timesTooManySamples = ((numSamplesToRead - 1) >> kSamplesToReadPerColMagnitude) + 1;
	if (timesTooManySamples > 1) {
		// If stereo, force an odd stride so we alternate between reading both channels.
		if (numChannels == 2 && (timesTooManySamples & 1) == 0) {
			timesTooManySamples++;
		}
		byteIncrement *= timesTooManySamples;
	}

	// Misalign, to align with non-32-bit data (the bytes before the logical start are valid padding).
	int32_t bytePos = startByte + byteDepth - 4;
	int32_t endPos = endByte + byteDepth - 4;

	WaveformPeak peak{std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::min()};
	for (; bytePos < endPos; bytePos += byteIncrement) {
		int32_t value = *reinterpret_cast<const int32_t*>(&clusterData[bytePos]);
		peak.min = std::min(peak.min, value);
		peak.max = std::max(peak.max, value);
	}
	return peak;
}

int8_t toCoarsePeak(int32_t value) {
	int8_t coarse = static_cast<int8_t>(value >> 24);
	if (value < 0) {
		coarse++; // round toward zero
	}
	return coarse;
}

int32_t lastAudioClusterEndByte(uint64_t numValidBytes, uint32_t audioDataStartPosBytes, int32_t clusterSize) {
	uint64_t totalEnd = numValidBytes + audioDataStartPosBytes;
	return static_cast<int32_t>(((totalEnd - 1) & static_cast<uint64_t>(clusterSize - 1)) + 1);
}

int64_t clusterStartByte(int32_t clusterIndex, int32_t sizeMagnitude) {
	return static_cast<int64_t>(clusterIndex) << sizeMagnitude;
}

int32_t firstFrameStartWithinCluster(int64_t clusterStartByteAbs, uint32_t audioDataStartPosBytes, int32_t frameSize) {
	if (clusterStartByteAbs <= static_cast<int64_t>(audioDataStartPosBytes)) {
		// This cluster still contains the file header; audio data begins at audioDataStartPosBytes.
		return static_cast<int32_t>(audioDataStartPosBytes - clusterStartByteAbs);
	}
	int32_t rem = static_cast<int32_t>((clusterStartByteAbs - audioDataStartPosBytes) % frameSize);
	return (rem == 0) ? 0 : (frameSize - rem);
}
