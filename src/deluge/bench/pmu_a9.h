#pragma once

/// @file
/// @brief The Cortex-A9 performance monitor as the benchmark's counters. Device only: the build compiles
/// pmu_a9.cpp for the device image and never for a host.

#include "bench/counters.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::bench {

/// @brief The PMU event number each Event counts, indexed by Event; event counter i counts entry i.
/// @details From the Cortex-A9 TRM (r4p1), "Performance monitoring events":
/// - L1D_REFILL, 0x03: "Level 1 data cache refill" (architectural).
/// - INSTRUCTIONS, 0x68: "Instructions coming out of the core renaming stage" (A9 specific), the A9's instruction
///   count in place of the architectural 0x08. It counts instructions as they are renamed, so it also counts the few
///   renamed down a mispredicted path.
/// - DC_STALL, 0x61: "Data cache dependent stall cycles": cycles the core has an instruction it cannot issue while
///   the load/store unit has a linefill pending and no TLB request.
/// - ISSUE_EMPTY, 0x66: "Issue does not dispatch any instruction": cycles the issue stage dispatches nothing, empty
///   or blocked. (0x67, "Issue is empty", is the narrower count of empty cycles.)
/// - NEON, 0x74: "NEON instructions": NEON instructions going through the renaming stage.
inline constexpr std::array<uint32_t, kEventCount> kA9EventNumbers{0x03, 0x68, 0x61, 0x66, 0x74};

/// @brief The Cortex-A9 PMU: the cycle counter, undivided, and five event counters programmed with kA9EventNumbers.
/// @details Accesses CP15 directly, so it runs privileged; the app runs in SYS mode. The counters are free-running
/// and 32 bits wide; a reading's difference is taken modulo 2^32 (see Sample).
class PmuA9 final : public Counters {
public:
	/// @brief Programs the events, resets every counter to zero and starts them all, the cycle counter counting every
	/// cycle.
	void start() override;

	/// @brief Reads the cycle counter, then each event counter, after an `isb`, so that everything before the call
	/// has retired.
	/// @return The current reading.
	[[nodiscard]] Sample read() override;
};

} // namespace deluge::bench
