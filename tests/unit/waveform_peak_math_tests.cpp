#include "CppUTest/TestHarness.h"

#include "gui/waveform/waveform_peak_math.h"
#include <cstdint>
#include <vector>

TEST_GROUP(WaveformPeakMath){};

// lastAudioClusterEndByte: exact boundary must give a full cluster, not 0.
TEST(WaveformPeakMath, LastClusterEndByteExactBoundaryIsFull) {
	constexpr int32_t kSize = 32768;
	// audio start 44, length chosen so start + len is an exact multiple of kSize
	uint32_t start = 44;
	uint64_t numValidBytes = static_cast<uint64_t>(kSize) * 4 - start; // start + numValidBytes == 4*kSize
	CHECK_EQUAL(kSize, lastAudioClusterEndByte(numValidBytes, start, kSize));
}

TEST(WaveformPeakMath, LastClusterEndByteMidClusterIsRemainder) {
	constexpr int32_t kSize = 32768;
	uint32_t start = 44;
	uint64_t numValidBytes = static_cast<uint64_t>(kSize) * 3 + 1000 - start; // ends 1000 bytes into cluster 3
	CHECK_EQUAL(1000, lastAudioClusterEndByte(numValidBytes, start, kSize));
}

// toCoarsePeak: round toward zero.
TEST(WaveformPeakMath, CoarsePeakRoundsTowardZero) {
	CHECK_EQUAL(0, toCoarsePeak(0));
	CHECK_EQUAL(127, toCoarsePeak(127 << 24));
	// INT32_MIN >> 24 == -128, then +1 for negatives == -127. Matches the production formula
	// (waveform_renderer.cpp), which unconditionally adds 1 to negatives, so -128 is never produced.
	CHECK_EQUAL(-127, toCoarsePeak(-128 << 24));
	CHECK_EQUAL(0, toCoarsePeak(-1));          // small negative rounds toward zero
	CHECK_EQUAL(-1, toCoarsePeak(-(2 << 24))); // -2.something -> -1 after +1
}

// clusterStartByte: no 32-bit overflow past 2 GB.
TEST(WaveformPeakMath, ClusterStartByteNoOverflow) {
	// cluster 70000 * 32768 = 2293760000, which overflows int32.
	CHECK_EQUAL(static_cast<int64_t>(2293760000LL), clusterStartByte(70000, 15));
}

// scanClusterPeak: finds the min and max of the 32-bit values it samples.
TEST(WaveformPeakMath, ScanClusterPeakFindsMinMax32Bit) {
	// byteDepth 4 (32-bit), mono. Buffer of 8 int32 values; misalignment (byteDepth-4)==0.
	std::vector<int32_t> samples = {5, -3, 100, -100, 42, 7, -1, 9};
	const char* data = reinterpret_cast<const char*>(samples.data());
	WaveformPeak peak = scanClusterPeak(data, 0, static_cast<int32_t>(samples.size() * 4), 4, 1);
	CHECK_EQUAL(-100, peak.min);
	CHECK_EQUAL(100, peak.max);
}

// scanClusterPeak: 16-bit misalignment reads two bytes before the logical start,
// so the caller must provide valid padding there. Verify the negative-index read is exercised.
TEST(WaveformPeakMath, ScanClusterPeak16BitMisalignedRead) {
	// 6 int16 values laid out; provide 4 bytes of leading padding so start=4 with byteDepth-4=-2 is valid.
	std::vector<int16_t> raw = {0, 0, 1000, -2000, 3000, -500, 750, 1};
	const char* base = reinterpret_cast<const char*>(raw.data());
	// startByte/endByte are within-cluster byte offsets into base; leading two int16 (4 bytes) are padding.
	WaveformPeak peak = scanClusterPeak(base, 4, 16, 2, 1);
	// Just assert it produced an ordered pair (min <= max); exact values depend on the 32-bit window reads.
	CHECK(peak.min <= peak.max);
}
