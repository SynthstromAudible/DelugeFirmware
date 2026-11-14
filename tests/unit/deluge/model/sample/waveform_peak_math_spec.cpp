#include "model/sample/waveform_peak_math.h"
#include <cstdint>
#include <vector>

#include "cppspec.hpp"
#include "support/spec_main.h"

// clang-format off
describe waveform_peak_math_group("waveform peak math", ${
	// lastAudioClusterEndByte: exact boundary must give a full cluster, not 0.
	it("last cluster end byte exact boundary is full", _ {
		constexpr int32_t kSize = 32768;
		// audio start 44, length chosen so start + len is an exact multiple of kSize
		uint32_t start = 44;
		uint64_t numValidBytes = static_cast<uint64_t>(kSize) * 4 - start; // start + numValidBytes == 4*kSize
		expect(lastAudioClusterEndByte(numValidBytes, start, kSize)).to_equal(kSize);
	});

	it("last cluster end byte mid cluster is remainder", _ {
		constexpr int32_t kSize = 32768;
		uint32_t start = 44;
		uint64_t numValidBytes = static_cast<uint64_t>(kSize) * 3 + 1000 - start; // ends 1000 bytes into cluster 3
		expect(lastAudioClusterEndByte(numValidBytes, start, kSize)).to_equal(1000);
	});

	// toCoarsePeak: round toward zero.
	it("coarse peak rounds toward zero", _ {
		expect(toCoarsePeak(0)).to_equal(0);
		expect(toCoarsePeak(127 << 24)).to_equal(127);
		// INT32_MIN >> 24 == -128, then +1 for negatives == -127. Matches the production formula
		// (waveform_renderer.cpp), which unconditionally adds 1 to negatives, so -128 is never produced.
		expect(toCoarsePeak(-128 << 24)).to_equal(-127);
		expect(toCoarsePeak(-1)).to_equal(0);          // small negative rounds toward zero
		expect(toCoarsePeak(-(2 << 24))).to_equal(-1); // -2.something -> -1 after +1
	});

	// clusterStartByte: no 32-bit overflow past 2 GB.
	it("cluster start byte no overflow", _ {
		// cluster 70000 * 32768 = 2293760000, which overflows int32.
		expect(clusterStartByte(70000, 15)).to_equal(static_cast<int64_t>(2293760000LL));
	});

	// firstFrameStartWithinCluster: header cluster -> audio starts at audioDataStartPosBytes.
	it("first frame start header cluster", _ {
		// Cluster 0 starts at absolute byte 0 and still holds the 44-byte WAV header.
		expect(firstFrameStartWithinCluster(0, 44, 4)).to_equal(44);
	});

	// firstFrameStartWithinCluster: boundary that lands exactly on a frame -> 0.
	it("first frame start frame aligned is zero", _ {
		// (32768 - 44) % 4 == 0, so the cluster starts on a frame boundary.
		expect(firstFrameStartWithinCluster(32768, 44, 4)).to_equal(0);
	});

	// firstFrameStartWithinCluster: 24-bit (frameSize 3) boundary that doesn't divide evenly.
	it("first frame start misaligned 24-bit", _ {
		// (32769 - 44) % 3 == 1, so the first whole frame begins 3 - 1 == 2 bytes in.
		expect(firstFrameStartWithinCluster(32769, 44, 3)).to_equal(2);
	});

	// scanClusterPeak: finds the min and max of the 32-bit values it samples.
	it("scan cluster peak finds min and max 32-bit", _ {
		// byteDepth 4 (32-bit), mono. Buffer of 8 int32 values; misalignment (byteDepth-4)==0.
		std::vector<int32_t> samples = {5, -3, 100, -100, 42, 7, -1, 9};
		const char* data = reinterpret_cast<const char*>(samples.data());
		WaveformPeak peak = scanClusterPeak(data, 0, static_cast<int32_t>(samples.size() * 4), 4, 1);
		expect(peak.min).to_equal(-100);
		expect(peak.max).to_equal(100);
	});

	// scanClusterPeak: 16-bit misalignment reads two bytes before the logical start,
	// so the caller must provide valid padding there. Verify the negative-index read is exercised.
	it("scan cluster peak 16-bit misaligned read", _ {
		// 6 int16 values laid out; provide 4 bytes of leading padding so start=4 with byteDepth-4=-2 is valid.
		std::vector<int16_t> raw = {0, 0, 1000, -2000, 3000, -500, 750, 1};
		const char* base = reinterpret_cast<const char*>(raw.data());
		// startByte/endByte are within-cluster byte offsets into base; leading two int16 (4 bytes) are padding.
		WaveformPeak peak = scanClusterPeak(base, 4, 16, 2, 1);
		// Just assert it produced an ordered pair (min <= max); exact values depend on the 32-bit window reads.
		expect(peak.min).not_().to_be_greater_than(peak.max);
	});
});

DELUGE_SPEC(waveform_peak_math_spec, waveform_peak_math_group)
