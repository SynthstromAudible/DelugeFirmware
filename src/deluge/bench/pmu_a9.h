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
/// @details From the Cortex-A9 TRM (r4p1), "Performance monitoring events", Tables 11.5 and 11.6:
/// - L1D_REFILL, 0x03: "Data cache miss" (Table 11.5, the architectural events): an L1 data cache refill.
/// - INSTRUCTIONS, 0x68: "Instructions coming out of the core renaming stage" (Table 11.6, the A9 specific events).
///   Table 11.5 marks the architectural 0x08 not implemented and points here. It counts instructions as they are
///   renamed, so it also counts the few renamed down a mispredicted path.
/// - DC_STALL, 0x61: "Data cache dependent stall cycles": cycles the core has an instruction it cannot issue while
///   the load/store unit has a linefill pending and no TLB request.
/// - ISSUE_EMPTY, 0x66: "Issue does not dispatch any instruction": cycles the issue stage dispatches nothing, empty
///   or blocked. (0x67, "Issue is empty", is the narrower count of empty cycles.)
/// - NEON, 0x74: "NEON instructions": NEON instructions going through the renaming stage.
inline constexpr std::array<uint32_t, kEventCount> kA9EventNumbers{0x03, 0x68, 0x61, 0x66, 0x74};

/// @brief Reads DBGAUTHSTATUS, the debug authentication status register, through the CP14 interface:
/// `mrc p14, 0, <Rt>, c7, c14, 6`.
/// @details Its fields say which kinds of debug the authentication signals permit. SNE, bit 6, is set when
/// non-invasive debug is permitted in Secure PL1 modes: with the recommended external debug interface it is
/// (DBGEN or NIDEN) and (SPIDEN or SPNIDEN). NSNE, bit 2, is set when non-invasive debug is permitted in Non-secure
/// state (ARMv7-A ARM, DDI 0406C.b, Part C, "DBGAUTHSTATUS, Authentication Status register"; Cortex-A9 TRM,
/// DDI 0388I, Table 10.9 in 10.6). The PMU's event counters count only where non-invasive debug is permitted, so in
/// Secure state they need both halves: DBGEN or NIDEN high, and SPIDEN or SPNIDEN high (DDI 0406C.b, Part C,
/// "Non-invasive debug authentication" and its table "v7 Debug non-invasive debug authentication, with Security
/// Extensions"; the A9's signals are DDI 0388I 10.8, Table 10.12). Without them the event counters read zero while
/// PMCCNTR still counts, because PMCR.DP is 0 (DDI 0406C.b, "Effects of non-invasive debug authentication on the
/// Performance Monitors"). Printing the register beside a run lets a zero `inst` on `calibration_loop` be told from
/// a misprogrammed event.
/// @return The register's value.
[[nodiscard]] uint32_t read_dbgauthstatus();

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
