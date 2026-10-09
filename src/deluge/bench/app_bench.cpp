/// @file
/// @brief `deluge_app_bench`: the boot hook that runs the DSP benchmarks when the app is built with DELUGE_DSP_BENCH.

#include <libdeluge/app.h>

#if DELUGE_DSP_BENCH
#include "bench/cases.h"
#include "bench/pmu_a9.h"
#include "bench/runner.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <libdeluge/system.h>
#include <string_view>
#include <tuple>

namespace {

/// @brief CPSR.I, the IRQ mask bit: bit 7, set while IRQs are masked (ARMv7-A ARM, DDI 0406C.b, B1.3.3).
constexpr uint32_t kCpsrIrqMask = uint32_t{1} << 7;

/// @return True if IRQs are masked: CPSR.I, read with `mrs`.
[[nodiscard]] bool irqs_masked() {
	uint32_t cpsr = 0;
	asm volatile("mrs %0, cpsr" : "=r"(cpsr));
	return (cpsr & kCpsrIrqMask) != 0;
}

/// @brief Prints one line over the log channel, then its newline.
/// @details deluge_log takes a NUL-terminated string, and a line is a view into the runner's buffer, so it is copied
/// into one with room for the terminator.
void log_line(std::string_view line) {
	std::array<char, deluge::bench::kLineCapacity + 1> text{};
	line.copy(text.data(), deluge::bench::kLineCapacity);
	deluge_log(text.data());
	deluge_log("\n");
}

/// @brief Prints the debug authentication status, which says whether the event counters can count.
/// @details See read_dbgauthstatus().
void log_dbgauthstatus() {
	// A plain line first: a fault or hang in the CP14 read below then shows where the run stopped.
	log_line("bench: reading debug authentication");
	std::array<char, deluge::bench::kLineCapacity> buffer{};
	log_line(deluge::bench::format_info_line(buffer, "dbgauthstatus", deluge::bench::read_dbgauthstatus()));
}

} // namespace

extern "C" bool deluge_app_bench(void) noexcept {
	// The protocol times every case with IRQs masked, since an interrupt inside a timed run would land in its
	// counts. The bench masks them itself rather than rely on its caller, and restores the mask it found.
	const bool were_masked = irqs_masked();
	asm volatile("cpsid i" ::: "memory");
	log_dbgauthstatus();
	deluge::bench::PmuA9 pmu;
	std::ignore =
	    deluge::bench::run(deluge::bench::cases(), {.block_sizes = deluge::bench::kBlockSizes}, pmu, log_line);
	if (!were_masked) {
		asm volatile("cpsie i" ::: "memory");
	}
	return true;
}

#else

extern "C" bool deluge_app_bench(void) noexcept {
	return false;
}

#endif
