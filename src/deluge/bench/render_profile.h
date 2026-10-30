#pragma once

/// @file
/// @brief The stream bench's render splits: PMU cycles in each Voice::render, in its patching, and in each
/// Sound's patching, summed per stream-bench phase. Compiled to nothing outside a device stream-bench image.

#include <cstdint>

namespace deluge::bench::render_profile {

/// Cycle sums for one phase. `voice_patch` is measured inside `Voice::render`, so it is part of `voice_render`.
struct Totals {
	uint64_t voice_render;
	uint64_t voice_patch;
	uint64_t sound_patch;
	uint32_t voice_blocks;
	uint32_t sound_blocks;
};

extern Totals g_totals;

#if defined(DELUGE_STREAM_BENCH) && defined(__arm__)
[[nodiscard]] [[gnu::always_inline]] inline uint32_t cycles() {
	uint32_t v;
	asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(v));
	return v;
}
/// PMCR.E on, PMCR.D off (count every cycle), the cycle counter enabled.
inline void enable() {
	uint32_t pmcr;
	asm volatile("mrc p15, 0, %0, c9, c12, 0" : "=r"(pmcr));
	pmcr = (pmcr | 1U) & ~(1U << 3);
	asm volatile("mcr p15, 0, %0, c9, c12, 0" ::"r"(pmcr));
	asm volatile("mcr p15, 0, %0, c9, c12, 1" ::"r"(1U << 31));
}
#else
[[nodiscard]] inline uint32_t cycles() {
	return 0;
}
inline void enable() {
}
#endif

} // namespace deluge::bench::render_profile
