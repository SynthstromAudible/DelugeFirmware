# #4460 Waveform Pre-scan Review Fixes — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix all correctness, performance, and duplication findings from the code review of the #4460 waveform overview pre-scan, without regressing zoomed-out single-row rendering.

**Architecture:** Extract the two pure, reusable pieces of `findPeaksPerCol`'s whole-cluster investigation — the sub-sampled peak read loop and the coarse int8 store — into a new dependency-free unit (`waveform_peak_math`). Both the render path and the background pre-scan call these, which removes the duplication and fixes the inverted-sentinel bug at its root. The remaining fixes (64-bit offset widening, shrink invalidation, scan throttling, idle short-circuit, cursor init) are surgical edits to the render path and the audio-idle scan.

**Tech Stack:** C++23, CMake + Ninja, CppUTest (x86 `-m32` host unit tests), SCons-based firmware build via `./dbt`.

## Global Constraints

- The new `waveform_peak_math` unit MUST have **no** dependencies on `Cluster`, `Sample`, `AudioEngine`, `audioFileManager`, or any hardware/display type — only `<cstdint>` and standard headers — so it compiles cleanly into the `-m32` unit-test target.
- Peak read behavior (sub-sampling stride, stereo odd-stride, `byteDepth − 4` misalignment) MUST remain **bit-identical** to the current `findPeaksPerCol` read loop for normal-sized files. Only the exact-cluster-boundary and >2 GB edge cases change.
- Coarse int8 peaks round **toward zero** (`x >> 24`, then `+1` if negative), matching current behavior.
- Unit tests: configure with `cmake -B build-tests -S tests -G Ninja`, build with `cmake --build build-tests`, run the group with `./build-tests/unit/UnitTests -g WaveformPeakMath` and the full suite with `ctest --test-dir build-tests --output-on-failure`.
- Firmware build for integration verification: `./dbt build` (use the project's standard OLED release target).

---

## File Structure

- **Create** `src/deluge/gui/waveform/waveform_peak_math.h` — pure declarations + shared constant.
- **Create** `src/deluge/gui/waveform/waveform_peak_math.cpp` — pure implementations.
- **Create** `tests/unit/waveform_peak_math_tests.cpp` — CppUTest coverage for the pure helpers.
- **Modify** `tests/unit/CMakeLists.txt` — add the new source + test to the build.
- **Modify** `src/deluge/gui/waveform/waveform_renderer.cpp` — call shared helpers; 64-bit offsets; shrink invalidation; throttle scan; empty-span fix.
- **Modify** `src/deluge/gui/waveform/waveform_render_data.h` — add cached valid-length field.
- **Modify** `src/deluge/storage/audio/audio_file_manager.cpp` / `.h` — throttle + idle short-circuit.
- **Modify** `src/deluge/model/sample/sample.cpp` / `.h` — cursor init consistency + per-sample done flag.

---

## Task 1: Pure `waveform_peak_math` unit with unit tests

Establishes the shared, testable helpers that Tasks 2–4 consume.

**Files:**
- Create: `src/deluge/gui/waveform/waveform_peak_math.h`
- Create: `src/deluge/gui/waveform/waveform_peak_math.cpp`
- Create: `tests/unit/waveform_peak_math_tests.cpp`
- Modify: `tests/unit/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `struct WaveformPeak { int32_t min; int32_t max; };`
  - `WaveformPeak scanClusterPeak(const char* clusterData, int32_t startByte, int32_t endByte, int32_t byteDepth, int32_t numChannels);` (param type is `const char*` to match `Cluster::data`, which is `char[]`.)
  - `int8_t toCoarsePeak(int32_t value);`
  - `int32_t lastAudioClusterEndByte(uint64_t numValidBytes, uint32_t audioDataStartPosBytes, int32_t clusterSize);`
  - `int64_t clusterStartByte(int32_t clusterIndex, int32_t sizeMagnitude);`
  - `constexpr int32_t kSamplesToReadPerColMagnitude = 9;`

- [ ] **Step 1: Write the header**

Create `src/deluge/gui/waveform/waveform_peak_math.h`:

```cpp
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
```

- [ ] **Step 2: Write the failing tests**

Create `tests/unit/waveform_peak_math_tests.cpp`:

```cpp
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
	CHECK_EQUAL(-128, toCoarsePeak(-128 << 24));
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
```

- [ ] **Step 3: Register the test + source in CMake**

In `tests/unit/CMakeLists.txt`, add the firmware source to the `deluge_SOURCES` glob list (after the `clock_output_scheduler.cpp` line):

```cmake
        # For waveform peak math tests
        ../../src/deluge/gui/waveform/waveform_peak_math.cpp
```

And add the test file to the `add_executable(UnitTests ...)` list (after `clock_output_scheduler_tests.cpp`):

```cmake
        waveform_peak_math_tests.cpp
```

- [ ] **Step 4: Run tests to verify they fail (link error / missing impl)**

Run:
```bash
cmake -B build-tests -S tests -G Ninja && cmake --build build-tests
```
Expected: build FAILS to link `UnitTests` with undefined references to `scanClusterPeak`, `toCoarsePeak`, `lastAudioClusterEndByte`, `clusterStartByte` (the `.cpp` is empty/absent).

- [ ] **Step 5: Write the implementation**

Create `src/deluge/gui/waveform/waveform_peak_math.cpp`:

```cpp
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
```

- [ ] **Step 6: Run tests to verify they pass**

Run:
```bash
cmake --build build-tests && ./build-tests/unit/UnitTests -g WaveformPeakMath
```
Expected: all `WaveformPeakMath` tests PASS; "OK (... tests, ... ran ...)".

- [ ] **Step 7: Commit**

```bash
git add src/deluge/gui/waveform/waveform_peak_math.h src/deluge/gui/waveform/waveform_peak_math.cpp \
        tests/unit/waveform_peak_math_tests.cpp tests/unit/CMakeLists.txt
git commit -m "feat(waveform): add pure peak-math helpers with unit tests (#4460)"
```

---

## Task 2: Route `findPeaksPerCol` through the shared helpers

Removes the render-path duplication (#6, render side) and applies the corrected `limit` (part of #1) with no behavior change for normal files.

**Files:**
- Modify: `src/deluge/gui/waveform/waveform_renderer.cpp` (`findPeaksPerCol`, lines 46, ~426-433, ~509-579)

**Interfaces:**
- Consumes: `scanClusterPeak`, `toCoarsePeak`, `lastAudioClusterEndByte`, `kSamplesToReadPerColMagnitude` from Task 1.

- [ ] **Step 1: Include the helper and drop the local magic-number define**

At the top includes of `waveform_renderer.cpp`, add:
```cpp
#include "gui/waveform/waveform_peak_math.h"
```
Delete the local define (line 46):
```cpp
#define SAMPLES_TO_READ_PER_COL_MAGNITUDE 9
```

- [ ] **Step 2: Use the corrected last-cluster limit**

Replace the block at lines 426-433:
```cpp
		else if (clusterIndexToDo == endClusters - 1) {

			int32_t limit = (numValidBytes + sample->audioDataStartPosBytes) & (Cluster::size - 1);

			if (endByteWithinCluster > limit) {
				endByteWithinCluster = limit;
			}
		}
```
with:
```cpp
		else if (clusterIndexToDo == endClusters - 1) {

			int32_t limit = lastAudioClusterEndByte(numValidBytes, sample->audioDataStartPosBytes, Cluster::size);

			if (endByteWithinCluster > limit) {
				endByteWithinCluster = limit;
			}
		}
```

- [ ] **Step 3: Replace the inline read loop with `scanClusterPeak`**

Replace lines 505-549 (from `numBytesToRead = endByteWithinCluster - startByteWithinCluster;` through the `while` read loop that ends at `bytePos += byteIncrement; }`) with:
```cpp
			numBytesToRead = endByteWithinCluster - startByteWithinCluster;

			// NOTE: from here on, we read *both* channels (if there are two), counting each one as a "sample"
			WaveformPeak peak = scanClusterPeak(cluster->data, startByteWithinCluster, endByteWithinCluster,
			                                    sample->byteDepth, sample->numChannels);
			int32_t minThisCol = peak.min;
			int32_t maxThisCol = peak.max;
```

- [ ] **Step 4: Replace the whole-cluster int8 store with `toCoarsePeak`**

Replace the whole-cluster branch body at lines 552-579:
```cpp
			// If we just looked at the length of one entire cluster...
			if (investigatingAWholeCluster) {

				// See if we want to include any previously captured maximums and minimums, which might have looked at
				// slightly different values
				int32_t prevMin = (int32_t)sampleCluster->minValue << 24;
				int32_t prevMax = (int32_t)sampleCluster->maxValue << 24;

				if (prevMin < minThisCol) {
					minThisCol = prevMin;
				}
				if (prevMax > maxThisCol) {
					maxThisCol = prevMax;
				}

				// And mark the SampleCluster as fully investigated
				sampleCluster->minValue = minThisCol >> 24;
				sampleCluster->maxValue = maxThisCol >> 24;

				// Make rounding be towards 0
				if (sampleCluster->minValue < 0) {
					sampleCluster->minValue++;
				}
				if (sampleCluster->maxValue < 0) {
					sampleCluster->maxValue++;
				}

				sampleCluster->investigatedWholeLength = true;
			}
```
with:
```cpp
			// If we just looked at the length of one entire cluster...
			if (investigatingAWholeCluster) {

				// See if we want to include any previously captured maximums and minimums, which might have looked at
				// slightly different values
				int32_t prevMin = (int32_t)sampleCluster->minValue << 24;
				int32_t prevMax = (int32_t)sampleCluster->maxValue << 24;

				if (prevMin < minThisCol) {
					minThisCol = prevMin;
				}
				if (prevMax > maxThisCol) {
					maxThisCol = prevMax;
				}

				// And mark the SampleCluster as fully investigated (rounding toward 0)
				sampleCluster->minValue = toCoarsePeak(minThisCol);
				sampleCluster->maxValue = toCoarsePeak(maxThisCol);
				sampleCluster->investigatedWholeLength = true;
			}
```

- [ ] **Step 5: Replace the partial-cluster int8 store with `toCoarsePeak`**

Replace the partial-cluster branch at lines 585-594:
```cpp
				// Then just contribute to the running record of max and min found
				int8_t smallMin = minThisCol >> 24;
				int8_t smallMax = maxThisCol >> 24;

				// Make rounding be towards 0
				if (smallMin < 0) {
					smallMin++;
				}
				if (smallMax < 0) {
					smallMax++;
				}
```
with:
```cpp
				// Then just contribute to the running record of max and min found
				int8_t smallMin = toCoarsePeak(minThisCol);
				int8_t smallMax = toCoarsePeak(maxThisCol);
```

- [ ] **Step 6: Update the remaining `SAMPLES_TO_READ_PER_COL_MAGNITUDE` reference**

Search the file for any remaining `SAMPLES_TO_READ_PER_COL_MAGNITUDE` (the old inline read loop used it at line ~513; it is now removed with Step 3). Confirm none remain:
```bash
grep -n "SAMPLES_TO_READ_PER_COL_MAGNITUDE" src/deluge/gui/waveform/waveform_renderer.cpp
```
Expected: no output. If any remain, replace with `kSamplesToReadPerColMagnitude`.

- [ ] **Step 7: Build firmware and unit tests**

Run:
```bash
cmake --build build-tests && ./build-tests/unit/UnitTests && ./dbt build
```
Expected: unit tests PASS; firmware build succeeds. (Render output for normal files is unchanged — the read loop and rounding are the same helpers, verified by Task 1.)

- [ ] **Step 8: Commit**

```bash
git add src/deluge/gui/waveform/waveform_renderer.cpp
git commit -m "refactor(waveform): findPeaksPerCol uses shared peak helpers + rounded last-cluster limit (#4460)"
```

---

## Task 3: Route the pre-scan through the shared helpers and fix the empty-span stamp

Fixes the inverted-sentinel bug at the pre-scan (#1) and removes the pre-scan duplication (#6, scan side).

**Files:**
- Modify: `src/deluge/gui/waveform/waveform_renderer.cpp` (`investigateWholeCluster`, and delete the anonymous-namespace `ClusterPeak`/`scanClusterPeak` added by the pre-scan)

**Interfaces:**
- Consumes: `scanClusterPeak`, `toCoarsePeak`, `lastAudioClusterEndByte`, `clusterStartByte` from Task 1.

- [ ] **Step 1: Delete the pre-scan's private `scanClusterPeak` copy**

Remove the anonymous-namespace block that defines `struct ClusterPeak` and the local `scanClusterPeak(const Cluster& ...)` (added by the working-tree change, immediately before `investigateWholeCluster`). The shared helper from Task 1 replaces it.

- [ ] **Step 2: Fix `investigateWholeCluster` to use shared helpers + not stamp on empty span**

In `investigateWholeCluster`, replace the byte-range computation and scan/store body. The offset math uses `clusterStartByte` (64-bit) and `lastAudioClusterEndByte`; the empty-span early-out must NOT set `investigatedWholeLength` unless data was scanned. Replace from the `clusterStartByteAbs` computation through the store, i.e.:

```cpp
	const int32_t frameSize = sample->numChannels * sample->byteDepth;
	const uint64_t numValidBytes = sample->lengthInSamples * sample->byteDepth * sample->numChannels;

	// Work out the first frame-aligned byte of audio data within this cluster.
	const int64_t clusterStartByteAbs = clusterStartByte(clusterIndex, Cluster::size_magnitude);
	int32_t startByteWithinCluster;
	if (clusterStartByteAbs <= static_cast<int64_t>(sample->audioDataStartPosBytes)) {
		// This cluster still contains the file header; audio data begins at audioDataStartPosBytes.
		startByteWithinCluster = static_cast<int32_t>(sample->audioDataStartPosBytes - clusterStartByteAbs);
	}
	else {
		const int32_t rem = static_cast<int32_t>((clusterStartByteAbs - sample->audioDataStartPosBytes) % frameSize);
		startByteWithinCluster = (rem == 0) ? 0 : (frameSize - rem);
	}

	int32_t endByteWithinCluster = Cluster::size;
	if (clusterIndex == endClusters - 1) {
		endByteWithinCluster =
		    std::min(endByteWithinCluster, lastAudioClusterEndByte(numValidBytes, sample->audioDataStartPosBytes, Cluster::size));
	}

	// Trim the end back to a whole frame so we never read past the cluster boundary.
	endByteWithinCluster -= (endByteWithinCluster - startByteWithinCluster) % frameSize;

	if (endByteWithinCluster <= startByteWithinCluster) {
		// No whole frames to scan here (e.g. a sub-frame tail). Leave it un-investigated rather than
		// stamping an inverted sentinel; advanceOverviewScan will skip past it.
		return true;
	}

	Cluster* cluster = sampleCluster->getCluster(sample, clusterIndex, CLUSTER_LOAD_IMMEDIATELY);
	if (cluster == nullptr) {
		return false; // Card busy / couldn't read - caller will retry later
	}

	WaveformPeak peak =
	    scanClusterPeak(cluster->data, startByteWithinCluster, endByteWithinCluster, sample->byteDepth, sample->numChannels);

	// Fold in anything previously captured for this cluster (same as findPeaksPerCol's whole-cluster path).
	peak.min = std::min(peak.min, static_cast<int32_t>(sampleCluster->minValue) << 24);
	peak.max = std::max(peak.max, static_cast<int32_t>(sampleCluster->maxValue) << 24);

	// Store the coarse (int8) overview, rounding toward 0.
	sampleCluster->minValue = toCoarsePeak(peak.min);
	sampleCluster->maxValue = toCoarsePeak(peak.max);
	sampleCluster->investigatedWholeLength = true;

	// Keep the sample's running peak up to date too, so brightness normalisation is pre-warmed.
	sample->maxValueFound = std::max(sample->maxValueFound, peak.max);
	sample->minValueFound = std::min(sample->minValueFound, peak.min);

	audioFileManager.removeReasonFromCluster(*cluster, "4460");
	AudioEngine::routineWithClusterLoading();

	return true;
```

Note the change from the working-tree version: the empty-span branch now `return true;` **without** setting `investigatedWholeLength`. This means `advanceOverviewScan` must treat such a cluster as "done for scanning" by cursor advance (it already advances the cursor after a `true` return), so the sub-frame tail is not retried forever — the cursor moves past it.

- [ ] **Step 3: Confirm no leftover references to the deleted `ClusterPeak`**

Run:
```bash
grep -n "ClusterPeak" src/deluge/gui/waveform/waveform_renderer.cpp
```
Expected: no output.

- [ ] **Step 4: Build firmware and unit tests**

Run:
```bash
cmake --build build-tests && ./build-tests/unit/UnitTests && ./dbt build
```
Expected: unit tests PASS; firmware builds.

- [ ] **Step 5: Manual verification checkpoint (record result in the commit/PR)**

On device or sim, load a sample whose `audioDataStartPosBytes + audioDataLengthBytes` is an exact multiple of the cluster size (e.g. a mono 16-bit WAV sized so total bytes align to 32768). In song row view, scroll to render its final cluster and confirm the last on-screen column renders a normal waveform bar (no spurious bright/inverted column). Before this fix it renders inverted.

- [ ] **Step 6: Commit**

```bash
git add src/deluge/gui/waveform/waveform_renderer.cpp
git commit -m "fix(waveform): pre-scan reuses shared helpers and never caches inverted sentinels (#4460)"
```

---

## Task 4: 64-bit byte-offset math in the render path

Fixes the >2 GB `int32_t` overflow (#2), including the pre-existing site at `findPeaksPerCol`.

**Files:**
- Modify: `src/deluge/gui/waveform/waveform_renderer.cpp` (`findPeaksPerCol` lines 340-345)

**Interfaces:**
- Consumes: (Task 1's `clusterStartByte` is already used in Task 3; this task widens the render path's own offsets.)

- [ ] **Step 1: Widen `colStartByte` / `colEndByte` to 64-bit**

Replace lines 340-345:
```cpp
		int32_t colStartByte =
		    colStartSample * sample->numChannels * sample->byteDepth + sample->audioDataStartPosBytes;
		int32_t colEndByte = colEndSample * sample->numChannels * sample->byteDepth + sample->audioDataStartPosBytes;

		int32_t colStartCluster = colStartByte >> Cluster::size_magnitude;
		int32_t colEndCluster = colEndByte >> Cluster::size_magnitude;
```
with:
```cpp
		int64_t colStartByte = static_cast<int64_t>(colStartSample) * sample->numChannels * sample->byteDepth
		                       + sample->audioDataStartPosBytes;
		int64_t colEndByte = static_cast<int64_t>(colEndSample) * sample->numChannels * sample->byteDepth
		                     + sample->audioDataStartPosBytes;

		int32_t colStartCluster = static_cast<int32_t>(colStartByte >> Cluster::size_magnitude);
		int32_t colEndCluster = static_cast<int32_t>(colEndByte >> Cluster::size_magnitude);
```

- [ ] **Step 2: Cast the within-cluster byte derivations back to int32**

The `& (Cluster::size - 1)` expressions that consume `colStartByte`/`colEndByte` (lines 358-359, 365, 374, 392, 395) now operate on `int64_t`. Each is assigned into an `int32_t` within-cluster variable and is `< Cluster::size`, so wrap each in `static_cast<int32_t>(...)`. For example at lines 358-359:
```cpp
			startByteWithinCluster = static_cast<int32_t>(colStartByte & (Cluster::size - 1));
			endByteWithinCluster = static_cast<int32_t>(colEndByte & (Cluster::size - 1));
```
Apply the same `static_cast<int32_t>(...)` wrap at lines 365 (`colStartByte & (Cluster::size - 1)`), 374 (`startByteWithinFirstCluster`), 392 (`startByteWithinFirstCluster`), and 395 (`bytesInSecondCluster`). The `colStartByte < (Cluster::size >> 1)` comparison at line 363 needs no change (int64 vs int constant compares fine).

- [ ] **Step 3: Build firmware and unit tests**

Run:
```bash
cmake --build build-tests && ./build-tests/unit/UnitTests && ./dbt build
```
Expected: unit tests PASS (including `WaveformPeakMath, ClusterStartByteNoOverflow`); firmware builds; no narrowing warnings.

- [ ] **Step 4: Commit**

```bash
git add src/deluge/gui/waveform/waveform_renderer.cpp
git commit -m "fix(waveform): compute waveform byte offsets in 64-bit to avoid >2GB overflow (#4460)"
```

---

## Task 5: Fix A shrink invariant + shift-count guard

Prevents stale `INVESTIGATED` columns rendering beyond a shrunk waveform (#3) and guards the shift-count narrowing.

**Files:**
- Modify: `src/deluge/gui/waveform/waveform_render_data.h`
- Modify: `src/deluge/gui/waveform/waveform_renderer.cpp` (`findPeaksPerCol` top, ~270-301)

**Interfaces:**
- Produces: `WaveformRenderData::validLengthSamples` field consumed only within `findPeaksPerCol`.

- [ ] **Step 1: Add a cached valid-length field**

In `waveform_render_data.h`, add to the struct (after `int64_t xZoom;`):
```cpp
	int64_t validLengthSamples; // Cached numValidSamples from the last render, to detect the waveform shrinking (#4460)
```

- [ ] **Step 2: Reorder so numValidSamples is known before the reuse decision, and invalidate on shrink/guard the shift count**

Replace the top of `findPeaksPerCol` (lines 270-301) so `numValidSamples`/`endClusters`/`numValidBytes` are computed first, then the invalidate/shift decision uses them:
```cpp
	uint64_t numValidSamples;
	int32_t endClusters;
	if (recorder) {
		numValidSamples = recorder->numSamplesCaptured;
		endClusters = sample->clusters.getNumElements();
	}
	else {
		numValidSamples = sample->lengthInSamples;
		endClusters = sample->getFirstClusterIndexWithNoAudioData();
	}

	uint64_t numValidBytes = numValidSamples * sample->byteDepth * sample->numChannels;

	if (xZoomSamples != data->xZoom || static_cast<int64_t>(numValidSamples) != data->validLengthSamples) {
		// Zoom changed, or the waveform length changed (e.g. sample replaced/shrunk): nothing is reusable.
		std::ranges::fill(data->colStatus, COL_STATUS_NOT_INVESTIGATED);
	}
	else if (xScrollSamples != data->xScroll) {
		// Scroll changed but zoom and length didn't. If we moved by a whole number of columns, the
		// overlapping columns' peaks are still valid - slide them across so only the newly-exposed
		// columns get re-investigated (fix A, issue #4460). Otherwise fall back to a full invalidate.
		int64_t deltaSamples = xScrollSamples - data->xScroll;
		int64_t shiftCols =
		    (deltaSamples % static_cast<int64_t>(xZoomSamples) == 0) ? deltaSamples / static_cast<int64_t>(xZoomSamples)
		                                                             : 0;
		// Whole-column shift only when it fits int32 and is a whole number of columns; else full invalidate.
		if (shiftCols != 0 && shiftCols >= -kDisplayWidth && shiftCols <= kDisplayWidth) {
			shiftRenderCache(*data, static_cast<int32_t>(shiftCols));
		}
		else if (deltaSamples % static_cast<int64_t>(xZoomSamples) != 0
		         || shiftCols < -kDisplayWidth || shiftCols > kDisplayWidth) {
			std::ranges::fill(data->colStatus, COL_STATUS_NOT_INVESTIGATED);
		}
	}

	data->xScroll = xScrollSamples;
	data->xZoom = xZoomSamples;
	data->validLengthSamples = static_cast<int64_t>(numValidSamples);

	bool hadAnyTroubleLoading = false;
```

Then delete the now-duplicated original computation of `numValidSamples`/`endClusters`/`numValidBytes` that followed (old lines 290-301), since it has been moved up.

- [ ] **Step 3: Build firmware and unit tests**

Run:
```bash
cmake --build build-tests && ./build-tests/unit/UnitTests && ./dbt build
```
Expected: unit tests PASS; firmware builds.

- [ ] **Step 4: Commit**

```bash
git add src/deluge/gui/waveform/waveform_render_data.h src/deluge/gui/waveform/waveform_renderer.cpp
git commit -m "fix(waveform): invalidate render cache when waveform length changes; guard shift count (#4460)"
```

---

## Task 6: Throttle the background scan's synchronous I/O

Stops the scan from chaining multiple synchronous card reads per idle tick against playback (#4).

**Files:**
- Modify: `src/deluge/storage/audio/audio_file_manager.h`
- Modify: `src/deluge/gui/waveform/waveform_renderer.cpp` (`advanceOverviewScan`)

**Interfaces:**
- Consumes: nothing new.

- [ ] **Step 1: Reduce the per-call budget to 1**

In `audio_file_manager.h`, change:
```cpp
	static constexpr int32_t kOverviewScanClustersPerCall = 4;
```
to:
```cpp
	// One cluster per idle tick: each investigate can trigger a synchronous card read, so keep this at 1
	// to avoid chaining reads against playback streaming (#4460).
	static constexpr int32_t kOverviewScanClustersPerCall = 1;
```

- [ ] **Step 2: Re-check the card/audio guard between investigations**

In `advanceOverviewScan` (waveform_renderer.cpp), inside the scan loop, bail out if the card/audio become busy mid-loop so a multi-cluster budget can never chain reads through a busy period. Replace the loop body's investigate call region:
```cpp
		if (!investigateWholeCluster(sample, clusterIndex)) {
			return true; // Card busy or still recording - retry this cluster next time without advancing
		}

		sample->overviewScanNextCluster++;
		budget--;
```
with:
```cpp
		// Bail if the card or audio routine became busy since entry, rather than issuing another
		// synchronous load this tick (#4460). `currentlyAccessingCard` is the file-scope extern already
		// declared at the top of this file; `clusterBeingLoaded` is a public AudioFileManager member.
		if (currentlyAccessingCard || (audioFileManager.clusterBeingLoaded != nullptr)
		    || AudioEngine::audioRoutineLocked) {
			return true;
		}

		if (!investigateWholeCluster(sample, clusterIndex)) {
			return true; // Card busy or still recording - retry this cluster next time without advancing
		}

		sample->overviewScanNextCluster++;
		budget--;
```

- [ ] **Step 3: Build firmware and unit tests**

Run:
```bash
cmake --build build-tests && ./build-tests/unit/UnitTests && ./dbt build
```
Expected: unit tests PASS; firmware builds. (`currentlyAccessingCard` is the file-scope `extern uint8_t` already declared at `waveform_renderer.cpp:38`; `clusterBeingLoaded` is a public `AudioFileManager` member — both accessible from `advanceOverviewScan`.)

- [ ] **Step 4: Commit**

```bash
git add src/deluge/storage/audio/audio_file_manager.h src/deluge/gui/waveform/waveform_renderer.cpp
git commit -m "fix(waveform): throttle background overview scan to one synchronous read per tick (#4460)"
```

---

## Task 7: Idle short-circuit + cursor-init consistency

Stops the scan re-walking all files forever once complete (#5) and makes the scan cursor start at the first audio cluster (#7).

**Files:**
- Modify: `src/deluge/model/sample/sample.h`
- Modify: `src/deluge/model/sample/sample.cpp`
- Modify: `src/deluge/storage/audio/audio_file_manager.h`
- Modify: `src/deluge/storage/audio/audio_file_manager.cpp` (`backgroundWaveformOverviewScan`, plus load/unload reset points)

**Interfaces:**
- Consumes: `advanceOverviewScan` return value (true == more work remains).

- [ ] **Step 1: Make the cursor init consistent**

Three sites currently disagree on the cursor's start (in-class `{0}`, a redundant constructor `= 0`, and `resetOverviewScan`'s `getFirstClusterIndexWithAudioData()`). Because `getFirstClusterIndexWithAudioData()` depends on `audioDataStartPosBytes`, which is not known at construction, keep the in-class default at `0` and make `advanceOverviewScan` seed the cursor to the first audio cluster on entry (self-healing, and never scans header-only clusters).

In `sample.h`, annotate the field:
```cpp
	int32_t overviewScanNextCluster{0}; // Seeded to the first audio cluster by advanceOverviewScan / resetOverviewScan (#4460)
```
In `sample.cpp` constructor, delete the now-redundant line:
```cpp
	overviewScanNextCluster = 0;
```
In `advanceOverviewScan` (waveform_renderer.cpp), add immediately before the existing upper-bound clamp of `overviewScanNextCluster`:
```cpp
	// Never scan header-only clusters: start at the first cluster that actually holds audio.
	sample->overviewScanNextCluster =
	    std::max(sample->overviewScanNextCluster, sample->getFirstClusterIndexWithAudioData());
```

- [ ] **Step 2: Add a manager-level all-done flag**

In `audio_file_manager.h`, add near `overviewScanFileIndex`:
```cpp
	bool overviewScanAllDone = false; // Set once every loaded sample is fully pre-scanned (#4460)
```

- [ ] **Step 3: Short-circuit the scan when everything is done**

In `backgroundWaveformOverviewScan` (audio_file_manager.cpp), at the top after the busy guards, add:
```cpp
	if (overviewScanAllDone) {
		return;
	}
```
And change the round-robin loop so that if a full pass finds no sample with work remaining, it sets the flag. Replace the loop:
```cpp
	// One sample's worth of work per call, round-robined so no single sample starves the others.
	for (int32_t tried = 0; tried < numFiles; tried++) {
		if (overviewScanFileIndex >= numFiles) {
			overviewScanFileIndex = 0;
		}
		AudioFile* audioFile = (AudioFile*)audioFiles.getElement(overviewScanFileIndex);
		overviewScanFileIndex++;

		if (!AudioFile::isSample(audioFile)) {
			continue;
		}

		// Advance this sample by a small budget. If there was real work to do, stop here for this call.
		Sample* sample = (Sample*)audioFile;
		if (waveformRenderer.advanceOverviewScan(sample, kOverviewScanClustersPerCall)) {
			return;
		}
	}
```
with:
```cpp
	// One sample's worth of work per call, round-robined so no single sample starves the others.
	bool anyWorkRemaining = false;
	for (int32_t tried = 0; tried < numFiles; tried++) {
		if (overviewScanFileIndex >= numFiles) {
			overviewScanFileIndex = 0;
		}
		AudioFile* audioFile = (AudioFile*)audioFiles.getElement(overviewScanFileIndex);
		overviewScanFileIndex++;

		if (!AudioFile::isSample(audioFile)) {
			continue;
		}

		// Advance this sample by a small budget. If there was real work to do, stop here for this call.
		Sample* sample = (Sample*)audioFile;
		if (waveformRenderer.advanceOverviewScan(sample, kOverviewScanClustersPerCall)) {
			anyWorkRemaining = true;
			return;
		}
	}

	// A full pass found nothing left to scan: go idle until something re-arms us.
	if (!anyWorkRemaining) {
		overviewScanAllDone = true;
	}
```

- [ ] **Step 4: Re-arm the flag when samples change**

The flag must be cleared whenever new scan work can appear: when a sample is loaded/added and when one is invalidated. In `audio_file_manager.cpp`, the audio-file load path inserts the new file at line 893 (`*error = audioFiles.insertElement(audioFile);`). Immediately after that insertion, add:
```cpp
	overviewScanAllDone = false; // New audio file may need pre-scanning (#4460)
```
(The other `insertElement` at line 566 is for a `WaveTable`, which the scan skips; re-arming there is unnecessary.)
And in `Sample::resetOverviewScan()` (sample.cpp), re-arm the manager via the existing global:
```cpp
	audioFileManager.overviewScanAllDone = false;
```
(Add `#include "storage/audio/audio_file_manager.h"` to `sample.cpp` if not already present.)

- [ ] **Step 5: Build firmware and unit tests**

Run:
```bash
cmake --build build-tests && ./build-tests/unit/UnitTests && ./dbt build
```
Expected: unit tests PASS; firmware builds.

- [ ] **Step 6: Manual verification checkpoint**

Load the long-song project from the issue. Confirm: (a) song row view scrolls smoothly under playback (the lag fix), and (b) after a few seconds of idle, the pre-scan has quiesced (no ongoing card activity attributable to the overview scan). Record the result in the PR.

- [ ] **Step 7: Commit**

```bash
git add src/deluge/model/sample/sample.h src/deluge/model/sample/sample.cpp \
        src/deluge/storage/audio/audio_file_manager.h src/deluge/storage/audio/audio_file_manager.cpp
git commit -m "fix(waveform): stop idle re-scan once complete; seed scan cursor at first audio cluster (#4460)"
```

---

## Final verification

- [ ] Run the full unit suite: `ctest --test-dir build-tests --output-on-failure` — expected all PASS.
- [ ] Firmware build: `./dbt build` — expected success.
- [ ] Manual repro (issue #4460 long-song project): smooth scrolling under playback; correct final-cluster rendering; no runaway idle card I/O.
- [ ] Review the full diff against the spec's finding list (#1–#7 + the two lower-severity items) and confirm each is addressed.

## Spec coverage check

- #1 inverted sentinel → Tasks 2 (limit) + 3 (empty-span never stamps).
- #2 int32 overflow → Task 4 (findPeaksPerCol) + Task 3 (pre-scan `clusterStartByte`).
- #3 shrink invariant + shift-count narrowing → Task 5.
- #4 blocking I/O → Task 6.
- #5 idle re-scan → Task 7.
- #6 duplication → Tasks 2 + 3 (shared helpers) + Task 1 (helper unit).
- #7 cursor init → Task 7.
- Lower-severity `shiftRenderCache` sub-range black-flash → deferred (noted in spec; not scheduled).
