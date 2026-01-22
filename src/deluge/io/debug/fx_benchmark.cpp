/*
 * Copyright © 2024-2025 Owlet Records
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 *
 * --- Additional terms under GNU GPL version 3 section 7 ---
 * This file requires preservation of the above copyright notice and author attribution
 * in all copies or substantial portions of this file.
 */

#include "io/debug/fx_benchmark.h"

#if ENABLE_FX_BENCHMARK

#include "io/debug/print.h"
#include "util/d_string.h"
#include <cstring>

namespace Debug {

FxBenchmark::FxBenchmark(const char* name, const char* tag1, const char* tag2, const char* tag3)
    : name_(name), tags_{tag1, tag2, tag3}, numTags_(0), startTime_(0), active_(false) {
	// Count non-null tags
	if (tag1 != nullptr) {
		numTags_++;
		if (tag2 != nullptr) {
			numTags_++;
			if (tag3 != nullptr) {
				numTags_++;
			}
		}
	}
}

void FxBenchmark::doStart() {
	// Ensure PMU cycle counter is enabled on first use
	static bool pmuInitialized = false;
	if (!pmuInitialized) {
		init(); // Enable PMU cycle counter
		pmuInitialized = true;
	}
	active_ = true;
	readCycleCounter(startTime_);
}

void FxBenchmark::doStop() {
	uint32_t endTime;
	readCycleCounter(endTime);
	uint32_t cycles = endTime - startTime_;
	// Queue result for deferred output - JSON output happens at endBuffer()
	FxBenchGlobal::queueResult(name_, cycles, startTime_, tags_, numTags_);
	active_ = false;
}

void FxBenchmark::outputJson(uint32_t cycles) {
	// Legacy - now using aggregated output via endBuffer()
	(void)cycles;
}

// Helper to append string
static char* appendStr(char* p, const char* src, char* end) {
	while (*src && p < end) {
		*p++ = *src++;
	}
	return p;
}

// Helper to append number
static char* appendNum(char* p, uint32_t num, char* end) {
	char numBuf[12];
	intToString(num, numBuf);
	return appendStr(p, numBuf, end);
}

void FxBenchGlobal::endBuffer() {
	// Output all queued results as compact CSV (lighter than JSON)
	// Format: B,fx,cycles,ts,tag1,tag2,tag3
	// 'B' prefix identifies benchmark lines (vs other debug output)
	char buffer[128];
	char* end = buffer + 120;

	for (uint8_t i = 0; i < numPending; ++i) {
		const FxBenchResult& r = pendingResults[i];
		char* p = buffer;

		*p++ = 'B';
		*p++ = ',';
		p = appendStr(p, r.name, end);
		*p++ = ',';
		p = appendNum(p, r.cycles, end);
		*p++ = ',';
		p = appendNum(p, r.ts, end);

		for (uint8_t j = 0; j < r.numTags; ++j) {
			*p++ = ',';
			p = appendStr(p, r.tags[j], end);
		}

		*p = '\0';
		println(buffer);
	}

	// Reset for next buffer
	numPending = 0;
	sampleThisBuffer = false;
}

} // namespace Debug

#endif // ENABLE_FX_BENCHMARK
