#pragma once

#include <cstdint>

// Pure helpers shared by the live waveform render path (findPeaksPerCol) and the
// background overview pre-scan (issue #4460). No engine/hardware dependencies so
// this unit is covered by host unit tests and cannot drift between the two callers.

constexpr int32_t kSamplesToReadPerColMagnitude = 9;

struct WaveformPeak {
	int32_t min;
	int32_t max;
};

// Sub-sampled min/max over [startByte, endByte) of a cluster's raw data buffer.
// Reproduces the historical read loop exactly: reads at most ~2^kSamplesToReadPerColMagnitude
// samples, forces an odd stride for stereo so both channels are sampled, and applies the
// `byteDepth - 4` misalignment that reads each sample's top bytes (the bytes before the
// logical start are valid Cluster padding, so clusterData may be indexed slightly negative).
// clusterData is `const char*` to match Cluster::data (declared `char data[]`).
WaveformPeak scanClusterPeak(const char* clusterData, int32_t startByte, int32_t endByte, int32_t byteDepth,
                             int32_t numChannels);

// Coarse int8 peak, rounded toward zero (matches the SampleCluster min/max storage).
int8_t toCoarsePeak(int32_t value);

// Exclusive end byte of audio within the final audio cluster. Rounds UP, so audio that ends
// exactly on a cluster boundary yields a full cluster (clusterSize) rather than 0 — the root
// fix for the inverted-sentinel bug. clusterSize must be a power of two.
int32_t lastAudioClusterEndByte(uint64_t numValidBytes, uint32_t audioDataStartPosBytes, int32_t clusterSize);

// Absolute byte offset of a cluster's start, in 64-bit so multi-GB samples don't overflow.
int64_t clusterStartByte(int32_t clusterIndex, int32_t sizeMagnitude);
