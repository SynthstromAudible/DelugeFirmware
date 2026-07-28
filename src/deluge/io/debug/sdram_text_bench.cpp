/*
 * Copyright © 2026 Owlet Records
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
#include "io/debug/sdram_text_bench.h"
#include "definitions.h"
#include "io/debug/print.h"
#include <algorithm>
#include <cstdint>
#include <cstring>

extern "C" {
#include "RZA1/cache/cache.h"
}

extern uint32_t __sdram_text_start;
extern uint32_t __sdram_text_end;

namespace {

// --- Kernels -------------------------------------------------------------------------------------
// Each kernel body is defined once and instantiated twice: one copy in regular .text (internal
// SRAM) and one in .sdram_text. The chain of operations is serially dependent so the compiler
// cannot fold or reorder it; noinline so each call is a real branch to the placed copy.

// Tight kernel: a few cache lines of code, loop-bound. Warm numbers should be placement-invariant
// (both run from L1I); cold numbers show the one-time fill cost of a small function.
#define TIGHT_KERNEL_BODY                                                                                              \
	{                                                                                                                  \
		uint32_t v = seed;                                                                                             \
		for (uint32_t i = 0; i < iters; i++) {                                                                         \
			v = v * 1664525u + 1013904223u;                                                                            \
			v ^= v >> 13;                                                                                              \
		}                                                                                                              \
		return v;                                                                                                      \
	}

// Sprawl kernel: ~48KB of straight-line code (4096 dependent steps), larger than the 32KB L1
// I-cache. Cold runs make every line miss, so the placement difference is the per-line
// instruction-fetch penalty of SDRAM vs internal SRAM - the number that decides what can move.
#define S1                                                                                                             \
	v = v * 1664525u + 1013904223u;                                                                                    \
	v ^= v >> 13;
#define S8 S1 S1 S1 S1 S1 S1 S1 S1
#define S64 S8 S8 S8 S8 S8 S8 S8 S8
#define S512 S64 S64 S64 S64 S64 S64 S64 S64
#define S4096 S512 S512 S512 S512 S512 S512 S512 S512

#define SPRAWL_KERNEL_BODY                                                                                             \
	{                                                                                                                  \
		(void)iters;                                                                                                   \
		uint32_t v = seed;                                                                                             \
		S4096                                                                                                          \
		return v;                                                                                                      \
	}

// clang-format off
// (the formatter cannot see through the body macros; its parse state only recovers at the first
// semicolon, so the off-region extends through the constants below)
[[gnu::noinline]] uint32_t tightInternal(uint32_t iters, uint32_t seed) TIGHT_KERNEL_BODY
[[gnu::noinline]] PLACE_SDRAM_TEXT uint32_t tightSdram(uint32_t iters, uint32_t seed) TIGHT_KERNEL_BODY

[[gnu::noinline]] uint32_t sprawlInternal(uint32_t iters, uint32_t seed) SPRAWL_KERNEL_BODY
[[gnu::noinline]] PLACE_SDRAM_TEXT uint32_t sprawlSdram(uint32_t iters, uint32_t seed) SPRAWL_KERNEL_BODY

// --- Measurement ---------------------------------------------------------------------------------

constexpr uint32_t kTrials = 9;
constexpr uint32_t kTightIters = 4096;
// clang-format on

void makeAllCachesCold() {
	// Global flush is heavy-handed but simple and fully defensible as "cold". Runs between timed
	// regions only. Briefly disturbs everything else running (audio may tick over a bump).
	L1_D_CacheWritebackFlushAll();
	L2CacheFlushAll();
	L1_I_CacheFlushAll();
	__asm__ volatile("dsb\nisb");
}

struct Stats {
	uint32_t min;
	uint32_t median;
};

uint32_t volatile sink; // Defeats dead-code elimination of kernel results

template <typename Fn>
Stats measure(Fn fn, bool cold) {
	uint32_t samples[kTrials];
	if (!cold) {
		sink = fn(kTightIters, 1); // Prime
	}
	for (uint32_t t = 0; t < kTrials; t++) {
		if (cold) {
			makeAllCachesCold();
		}
		uint32_t t0 = Debug::readCycleCounter();
		uint32_t r = fn(kTightIters, t + 1);
		uint32_t t1 = Debug::readCycleCounter();
		sink = r;
		samples[t] = t1 - t0;
	}
	std::sort(samples, samples + kTrials);
	return Stats{samples[0], samples[kTrials / 2]};
}

void printHex(char const* label, uint32_t value) {
	static char const digits[] = "0123456789ABCDEF";
	char buf[11] = "0x";
	for (int32_t i = 0; i < 8; i++) {
		buf[2 + i] = digits[(value >> (28 - i * 4)) & 0xF];
	}
	buf[10] = 0;
	Debug::print(label);
	Debug::print(buf);
}

void reportKernel(char const* name, char const* placement, Stats cold, Stats warm) {
	Debug::print("{\"bench\":\"sdram_text\",\"kernel\":\"");
	Debug::print(name);
	Debug::print("\",\"placement\":\"");
	Debug::print(placement);
	Debug::print("\",\"cold_min\":");
	Debug::print((int32_t)cold.min);
	Debug::print(",\"cold_med\":");
	Debug::print((int32_t)cold.median);
	Debug::print(",\"warm_min\":");
	Debug::print((int32_t)warm.min);
	Debug::print(",\"warm_med\":");
	Debug::print((int32_t)warm.median);
	Debug::println("}");
}

} // namespace

void sdramTextBenchReport() {
	static bool hasRun = false;
	if (hasRun) {
		return;
	}
	hasRun = true;

	Debug::init(); // Idempotent; guarantees the PMU cycle counter is enabled

	// Self-documenting layout line: proves at runtime where each copy actually lives.
	Debug::print("{\"bench\":\"sdram_text\",\"info\":\"layout\"");
	printHex(",\"sdram_text_start\":\"", (uint32_t)&__sdram_text_start);
	printHex("\",\"sdram_text_end\":\"", (uint32_t)&__sdram_text_end);
	printHex("\",\"tight_internal\":\"", (uint32_t)&tightInternal);
	printHex("\",\"tight_sdram\":\"", (uint32_t)&tightSdram);
	printHex("\",\"sprawl_internal\":\"", (uint32_t)&sprawlInternal);
	printHex("\",\"sprawl_sdram\":\"", (uint32_t)&sprawlSdram);
	Debug::println("\"}");

	// Marker before the first SDRAM-placed call: in a SDRAM_TEXT_SKIP_CACHE_FIX build, a crash
	// after this line and before the next result attributes the fault to SDRAM execution.
	Debug::println("{\"bench\":\"sdram_text\",\"info\":\"calling sdram-placed code next\"}");
	sink = tightSdram(1, 1);
	Debug::println("{\"bench\":\"sdram_text\",\"info\":\"sdram-placed code returned ok\"}");

	// Interleave placements so drift (interrupt load etc.) cancels rather than biasing one side.
	Stats tightIntCold = measure(tightInternal, true);
	Stats tightSdrCold = measure(tightSdram, true);
	Stats tightIntWarm = measure(tightInternal, false);
	Stats tightSdrWarm = measure(tightSdram, false);
	reportKernel("tight", "internal", tightIntCold, tightIntWarm);
	reportKernel("tight", "sdram", tightSdrCold, tightSdrWarm);

	Stats sprawlIntCold = measure(sprawlInternal, true);
	Stats sprawlSdrCold = measure(sprawlSdram, true);
	Stats sprawlIntWarm = measure(sprawlInternal, false);
	Stats sprawlSdrWarm = measure(sprawlSdram, false);
	reportKernel("sprawl", "internal", sprawlIntCold, sprawlIntWarm);
	reportKernel("sprawl", "sdram", sprawlSdrCold, sprawlSdrWarm);
}
