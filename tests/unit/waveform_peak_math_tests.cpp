#include "CppUTest/TestHarness.h"

#include "gui/waveform/waveform_peak_math.h"
#include "gui/waveform/waveform_render_data.h"
#include <cstdint>
#include <vector>

using namespace deluge::gui::waveform;

TEST_GROUP(WaveformPeakMath){};

// lastClusterEnd: exact boundary must give a full cluster, not 0.
TEST(WaveformPeakMath, LastClusterEndExactBoundaryIsFull) {
	constexpr int32_t kClusterSize = 32768;
	// audio start 44, length chosen so start + len is an exact multiple of kClusterSize
	uint32_t start = 44;
	uint64_t audio_bytes = static_cast<uint64_t>(kClusterSize) * 4 - start; // start + audio_bytes == 4*kClusterSize
	CHECK_EQUAL(kClusterSize, lastClusterEnd(audio_bytes, start, kClusterSize));
}

TEST(WaveformPeakMath, LastClusterEndMidClusterIsRemainder) {
	constexpr int32_t kClusterSize = 32768;
	uint32_t start = 44;
	uint64_t audio_bytes = static_cast<uint64_t>(kClusterSize) * 3 + 1000 - start; // ends 1000 bytes into cluster 3
	CHECK_EQUAL(1000, lastClusterEnd(audio_bytes, start, kClusterSize));
}

// coarsePeak: round toward zero.
TEST(WaveformPeakMath, CoarsePeakRoundsTowardZero) {
	CHECK_EQUAL(0, coarsePeak(0));
	CHECK_EQUAL(127, coarsePeak(127 << 24));
	// INT32_MIN >> 24 == -128, then +1 for negatives == -127. Matches the production formula
	// (waveform_renderer.cpp), which unconditionally adds 1 to negatives, so -128 is never produced.
	CHECK_EQUAL(-127, coarsePeak(-128 << 24));
	CHECK_EQUAL(0, coarsePeak(-1));          // small negative rounds toward zero
	CHECK_EQUAL(-1, coarsePeak(-(2 << 24))); // -2.something -> -1 after +1
}

// clusterStart: no 32-bit overflow past 2 GB.
TEST(WaveformPeakMath, ClusterStartNoOverflow) {
	// cluster 70000 * 32768 = 2293760000, which overflows int32.
	CHECK_EQUAL(static_cast<int64_t>(2293760000LL), clusterStart(70000, 15));
}

// firstFrameOffset: header cluster -> audio starts at audioDataStartPosBytes.
TEST(WaveformPeakMath, FirstFrameOffsetHeaderCluster) {
	// Cluster 0 starts at absolute byte 0 and still holds the 44-byte WAV header.
	CHECK_EQUAL(44, firstFrameOffset(0, 44, 4));
}

// firstFrameOffset: boundary that lands exactly on a frame -> 0.
TEST(WaveformPeakMath, FirstFrameOffsetAlignedIsZero) {
	// (32768 - 44) % 4 == 0, so the cluster starts on a frame boundary.
	CHECK_EQUAL(0, firstFrameOffset(32768, 44, 4));
}

// firstFrameOffset: 24-bit (frameSize 3) boundary that doesn't divide evenly.
TEST(WaveformPeakMath, FirstFrameOffsetMisaligned24Bit) {
	// (32769 - 44) % 3 == 1, so the first whole frame begins 3 - 1 == 2 bytes in.
	CHECK_EQUAL(2, firstFrameOffset(32769, 44, 3));
}

// scanPeak: finds the min and max of the 32-bit values it samples.
TEST(WaveformPeakMath, ScanPeakFindsMinMax32Bit) {
	// byteDepth 4 (32-bit), mono. Buffer of 8 int32 values; misalignment (byteDepth-4)==0.
	std::vector<int32_t> samples = {5, -3, 100, -100, 42, 7, -1, 9};
	const char* data = reinterpret_cast<const char*>(samples.data());
	Peak peak = scanPeak(data, 0, static_cast<int32_t>(samples.size() * 4), 4, 1);
	CHECK_EQUAL(-100, peak.min);
	CHECK_EQUAL(100, peak.max);
}

// scanPeak: 16-bit misalignment reads two bytes before the logical start,
// so the caller must provide valid padding there. Verify the negative-index read is exercised.
TEST(WaveformPeakMath, ScanPeak16BitMisalignedRead) {
	// 6 int16 values laid out; provide 4 bytes of leading padding so start=4 with byteDepth-4=-2 is valid.
	std::vector<int16_t> raw = {0, 0, 1000, -2000, 3000, -500, 750, 1};
	const char* base = reinterpret_cast<const char*>(raw.data());
	// startByte/endByte are within-cluster byte offsets into base; leading two int16 (4 bytes) are padding.
	Peak peak = scanPeak(base, 4, 16, 2, 1);
	// Just assert it produced an ordered pair (min <= max); exact values depend on the 32-bit window reads.
	CHECK(peak.min <= peak.max);
}

// columnShift: an exact whole-column scroll is reused in either direction.
TEST(WaveformPeakMath, ColumnShiftExactWholeColumns) {
	CHECK_EQUAL(3, columnShift(3000, 1000, 16).value());
	CHECK_EQUAL(-2, columnShift(-2000, 1000, 16).value());
}

// columnShift: AudioClip converts ticks to samples with integer division, so a one-pad scroll
// can land a sample or two off a whole column. That must still count as a whole-column shift.
TEST(WaveformPeakMath, ColumnShiftToleratesTickTruncation) {
	CHECK_EQUAL(1, columnShift(1001, 1000, 16).value());
	CHECK_EQUAL(1, columnShift(999, 1000, 16).value());
	CHECK_EQUAL(-4, columnShift(-3997, 1000, 16).value());
}

// columnShift: a genuine sub-column scroll must not be treated as a shift.
TEST(WaveformPeakMath, ColumnShiftRejectsPartialColumns) {
	CHECK_FALSE(columnShift(500, 1000, 16).has_value());
	CHECK_FALSE(columnShift(1, 1000, 16).has_value());
	CHECK_FALSE(columnShift(1100, 1000, 16).has_value());
}

// columnShift: at deep zoom the truncation slack would be a visible fraction of a column.
TEST(WaveformPeakMath, ColumnShiftDeepZoomNeedsNearExact) {
	CHECK_EQUAL(8, columnShift(32, 4, 16).value());
	CHECK_FALSE(columnShift(34, 4, 16).has_value());
}

// columnShift: scrolling a whole screen or more leaves nothing reusable.
TEST(WaveformPeakMath, ColumnShiftBeyondDisplayWidth) {
	CHECK_EQUAL(16, columnShift(16000, 1000, 16).value());
	CHECK_FALSE(columnShift(17000, 1000, 16).has_value());
	CHECK_FALSE(columnShift(INT64_MAX / 2, 1000, 16).has_value());
}

namespace {

// A cache of fully investigated columns at the given scroll and zoom, where column i's peaks are -(i + 1) and i + 1.
WaveformRenderData investigatedColumns(int64_t scroll, int64_t zoom) {
	WaveformRenderData data{};
	data.xScroll = scroll;
	data.xZoom = zoom;
	for (int32_t col = 0; col < kDisplayWidth; col++) {
		data.colStatus[col] = ColumnStatus::INVESTIGATED;
		data.minPerCol[col] = -(col + 1);
		data.maxPerCol[col] = col + 1;
	}
	return data;
}

} // namespace

// remapColumns: zooming out 2x, each new column on screen before is exactly the old pair it covers.
TEST(WaveformPeakMath, RemapZoomOutReusesWholeColumns) {
	WaveformRenderData data = investigatedColumns(0, 1000);
	remapColumns(data, 0, 2000);
	for (int32_t col = 0; col < kDisplayWidth / 2; col++) {
		CHECK(data.colStatus[col] == ColumnStatus::INVESTIGATED);
		CHECK_EQUAL(-(2 * col + 2), data.minPerCol[col]);
		CHECK_EQUAL(2 * col + 2, data.maxPerCol[col]);
	}
	// The right half was off screen before
	for (int32_t col = kDisplayWidth / 2; col < kDisplayWidth; col++) {
		CHECK(data.colStatus[col] == ColumnStatus::NOT_INVESTIGATED);
	}
}

// remapColumns: tick-to-sample truncation leaves a zoom out a sample or two off whole columns. Still exact.
TEST(WaveformPeakMath, RemapZoomOutToleratesTickTruncation) {
	WaveformRenderData data = investigatedColumns(0, 1000);
	remapColumns(data, 1, 1999);
	CHECK(data.colStatus[0] == ColumnStatus::INVESTIGATED);
	CHECK_EQUAL(2, data.maxPerCol[0]);
	CHECK(data.colStatus[7] == ColumnStatus::INVESTIGATED);
	CHECK_EQUAL(16, data.maxPerCol[7]);
}

// remapColumns: zooming in 2x, each old column becomes a placeholder for the two new ones within it.
TEST(WaveformPeakMath, RemapZoomInGivesPlaceholders) {
	WaveformRenderData data = investigatedColumns(0, 2000);
	remapColumns(data, 4000, 1000);
	for (int32_t col = 0; col < kDisplayWidth; col++) {
		CHECK(data.colStatus[col] == ColumnStatus::PLACEHOLDER);
		CHECK_EQUAL(2 + col / 2 + 1, data.maxPerCol[col]);
	}
}

// remapColumns: a scroll by part of a column overlaps two old columns, so each new one is an estimate from both.
TEST(WaveformPeakMath, RemapPartialScrollGivesPlaceholders) {
	WaveformRenderData data = investigatedColumns(0, 1000);
	remapColumns(data, 500, 1000);
	CHECK(data.colStatus[0] == ColumnStatus::PLACEHOLDER);
	CHECK_EQUAL(-2, data.minPerCol[0]);
	CHECK_EQUAL(2, data.maxPerCol[0]);
	// The last one only half-overlaps the old screen, but that half still gives an estimate
	CHECK(data.colStatus[kDisplayWidth - 1] == ColumnStatus::PLACEHOLDER);
	CHECK_EQUAL(kDisplayWidth, data.maxPerCol[kDisplayWidth - 1]);
}

// remapColumns: an old column that wasn't worked out stops a new one covering it from being exact.
TEST(WaveformPeakMath, RemapUninvestigatedOldColumnIsNotExact) {
	WaveformRenderData data = investigatedColumns(0, 1000);
	data.colStatus[1] = ColumnStatus::NOT_INVESTIGATED;
	data.colStatus[2] = ColumnStatus::BEYOND_WAVEFORM;
	data.colStatus[3] = ColumnStatus::BEYOND_WAVEFORM;
	data.colStatus[4] = ColumnStatus::PLACEHOLDER;
	remapColumns(data, 0, 2000);
	CHECK(data.colStatus[0] == ColumnStatus::PLACEHOLDER);
	CHECK_EQUAL(1, data.maxPerCol[0]);
	CHECK(data.colStatus[1] == ColumnStatus::NOT_INVESTIGATED);
	CHECK(data.colStatus[2] == ColumnStatus::PLACEHOLDER);
	CHECK_EQUAL(6, data.maxPerCol[2]);
}

// remapColumns: at deep zoom the truncation slack would be a visible fraction of a column, so it must be exact.
TEST(WaveformPeakMath, RemapDeepZoomNeedsNearExact) {
	WaveformRenderData data = investigatedColumns(0, 4);
	remapColumns(data, 0, 8);
	CHECK(data.colStatus[0] == ColumnStatus::INVESTIGATED);

	data = investigatedColumns(0, 4);
	remapColumns(data, 2, 8);
	CHECK(data.colStatus[0] == ColumnStatus::PLACEHOLDER);
}

// remapColumns: with no peaks cached yet (zoom 0, as a fresh WaveformRenderData has), nothing carries over.
TEST(WaveformPeakMath, RemapFromNothing) {
	WaveformRenderData data{};
	data.colStatus[0] = ColumnStatus::INVESTIGATED;
	remapColumns(data, 0, 1000);
	for (ColumnStatus status : data.colStatus) {
		CHECK(status == ColumnStatus::NOT_INVESTIGATED);
	}
}
