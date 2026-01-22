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

#pragma once

#include <cstdint>

#if ENABLE_FX_BENCHMARK

#include "io/debug/print.h"

namespace Debug {

// Default: sample every 3450 buffers (~10 seconds at 44.1kHz/128 samples)
inline constexpr uint32_t kFxBenchDefaultN = 3450;

// Maximum queued results per buffer (8 voices × ~5 effects × sub-aggregations)
inline constexpr uint32_t kMaxPendingResults = 128;

// Queued benchmark result - stored during audio processing, output at end of buffer
struct FxBenchResult {
	const char* name;
	uint32_t cycles;
	uint32_t ts;
	const char* tags[3];
	uint8_t numTags;
};

// Global sampling state - call tick() once per audio buffer from audio engine
// All benchmarks check this flag - minimal overhead (one bool check per call)
// Results are queued during processing and flushed at endBuffer() to avoid
// JSON output overhead during audio processing.
struct FxBenchGlobal {
	static inline bool sampleThisBuffer = false;
	static inline uint32_t counter = 0;
	static inline uint32_t interval = kFxBenchDefaultN;

	// Pending results queue - filled during audio processing, flushed at end
	static inline FxBenchResult pendingResults[kMaxPendingResults];
	static inline uint8_t numPending = 0;

	// Call once per audio buffer (from audio engine)
	static void tick() {
		if (++counter > interval) {
			sampleThisBuffer = true;
			counter = 0;
		}
	}

	// Queue a result for deferred output (called from doStop)
	static void queueResult(const char* name, uint32_t cycles, uint32_t ts, const char* const* tags, uint8_t numTags) {
		if (numPending < kMaxPendingResults) {
			FxBenchResult& r = pendingResults[numPending++];
			r.name = name;
			r.cycles = cycles;
			r.ts = ts;
			r.numTags = numTags;
			for (uint8_t i = 0; i < 3; ++i) {
				r.tags[i] = (i < numTags) ? tags[i] : nullptr;
			}
		}
	}

	// Flush all queued results (outputs JSON) and reset sampling flag
	static void endBuffer();
};

// FX Benchmark class - outputs JSON format cycle counts with optional tags
// Usage:
//   FX_BENCH_DECLARE(bench, "effect_name", "tag1", "tag2");
//   FX_BENCH_START(bench);
//   // ... effect code ...
//   FX_BENCH_STOP(bench);
//
// Output format: {"fx":"effect_name","cycles":12345,"ts":67890,"tags":["tag1","tag2"]}
class FxBenchmark {
public:
	// Constructor with name and up to 3 optional tags
	FxBenchmark(const char* name, const char* tag1 = nullptr, const char* tag2 = nullptr, const char* tag3 = nullptr);

	// Start timing (only if global sampling flag is set)
	void start() {
		if (FxBenchGlobal::sampleThisBuffer) {
			doStart();
		}
	}

	// Stop timing and output JSON if active
	void stop() {
		if (active_) {
			doStop();
		}
	}

	// Set/update a tag at runtime (index 0-2)
	void setTag(uint8_t index, const char* tag) {
		// Only update when we're going to sample
		if (FxBenchGlobal::sampleThisBuffer && index < 3) {
			tags_[index] = tag;
			if (tag != nullptr && index >= numTags_) {
				numTags_ = index + 1;
			}
		}
	}

private:
	const char* name_;
	const char* tags_[3];
	uint8_t numTags_;
	uint32_t startTime_;
	bool active_;

	void doStart();
	void doStop();
	void outputJson(uint32_t cycles);
};

// RAII scope guard for automatic start/stop
class FxBenchmarkScope {
public:
	explicit FxBenchmarkScope(FxBenchmark& bench) : bench_(bench) { bench_.start(); }
	~FxBenchmarkScope() { bench_.stop(); }

	FxBenchmarkScope(const FxBenchmarkScope&) = delete;
	FxBenchmarkScope& operator=(const FxBenchmarkScope&) = delete;

private:
	FxBenchmark& bench_;
};

} // namespace Debug

// Convenience macros for instrumentation

// Declare a static benchmark variable with name and optional tags
#define FX_BENCH_DECLARE(varname, name, ...) static Debug::FxBenchmark varname(name, ##__VA_ARGS__)

// Start/stop timing manually
#define FX_BENCH_START(varname) (varname).start()
#define FX_BENCH_STOP(varname) (varname).stop()

// RAII scope guard - times from declaration to end of scope
#define FX_BENCH_SCOPE(varname) Debug::FxBenchmarkScope _fx_bench_scope_##varname(varname)

// Set tag at runtime (call before FX_BENCH_START)
#define FX_BENCH_SET_TAG(varname, index, tag) (varname).setTag(index, tag)

// Call once per audio buffer to advance global sampling
#define FX_BENCH_TICK() Debug::FxBenchGlobal::tick()
#define FX_BENCH_END_BUFFER() Debug::FxBenchGlobal::endBuffer()

#else // !ENABLE_FX_BENCHMARK

// All macros compile away to nothing when benchmarking is disabled
#define FX_BENCH_DECLARE(varname, name, ...)
#define FX_BENCH_START(varname) ((void)0)
#define FX_BENCH_STOP(varname) ((void)0)
#define FX_BENCH_SCOPE(varname) ((void)0)
#define FX_BENCH_SET_TAG(varname, index, tag) ((void)0)
#define FX_BENCH_TICK() ((void)0)
#define FX_BENCH_END_BUFFER() ((void)0)

#endif // ENABLE_FX_BENCHMARK
