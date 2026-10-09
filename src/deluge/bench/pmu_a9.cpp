#include "bench/pmu_a9.h"

#include <cstddef>
#include <cstdint>

namespace deluge::bench {
namespace {

// PMCR, the control register: c9, c12, 0.
constexpr uint32_t kPmcrEnable = uint32_t{1} << 0;       ///< E: every counter enabled by PMCNTENSET counts.
constexpr uint32_t kPmcrEventReset = uint32_t{1} << 1;   ///< P: resets every event counter to zero.
constexpr uint32_t kPmcrCycleReset = uint32_t{1} << 2;   ///< C: resets the cycle counter to zero.
constexpr uint32_t kPmcrCycleDivider = uint32_t{1} << 3; ///< D: the cycle counter counts every 64th cycle. Left 0.

/// @brief PMCNTENSET's bit for the cycle counter; bit i enables event counter i.
constexpr uint32_t kCycleCounterBit = uint32_t{1} << 31;

/// @brief The bits of every counter this class uses: event counters 0 to kEventCount - 1, and the cycle counter.
constexpr uint32_t kCounterBits = ((uint32_t{1} << kEventCount) - 1) | kCycleCounterBit;

/// @brief Waits until every earlier instruction has retired and every earlier CP15 write has taken effect.
[[gnu::always_inline]] inline void isb() {
	asm volatile("isb" ::: "memory");
}

[[gnu::always_inline]] inline void write_pmcr(uint32_t value) {
	asm volatile("mcr p15, 0, %0, c9, c12, 0" : : "r"(value) : "memory");
}

[[gnu::always_inline]] inline void write_pmcntenset(uint32_t bits) {
	asm volatile("mcr p15, 0, %0, c9, c12, 1" : : "r"(bits) : "memory");
}

[[gnu::always_inline]] inline void write_pmcntenclr(uint32_t bits) {
	asm volatile("mcr p15, 0, %0, c9, c12, 2" : : "r"(bits) : "memory");
}

/// @brief Clears the overflow flags of the counters in @p bits (PMOVSR is write-one-to-clear).
[[gnu::always_inline]] inline void write_pmovsr(uint32_t bits) {
	asm volatile("mcr p15, 0, %0, c9, c12, 3" : : "r"(bits) : "memory");
}

/// @brief Selects event counter @p index for the next PMXEVTYPER or PMXEVCNTR access.
[[gnu::always_inline]] inline void write_pmselr(uint32_t index) {
	asm volatile("mcr p15, 0, %0, c9, c12, 5" : : "r"(index) : "memory");
}

/// @brief Sets the event the selected counter counts.
[[gnu::always_inline]] inline void write_pmxevtyper(uint32_t event) {
	asm volatile("mcr p15, 0, %0, c9, c13, 1" : : "r"(event) : "memory");
}

[[gnu::always_inline]] inline uint32_t read_pmccntr() {
	uint32_t value = 0;
	asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(value) : : "memory");
	return value;
}

/// @brief Reads the selected event counter.
[[gnu::always_inline]] inline uint32_t read_pmxevcntr() {
	uint32_t value = 0;
	asm volatile("mrc p15, 0, %0, c9, c13, 2" : "=r"(value) : : "memory");
	return value;
}

/// @brief Selects event counter @p index and reads it.
[[gnu::always_inline]] inline uint32_t read_event(uint32_t index) {
	write_pmselr(index);
	isb();
	return read_pmxevcntr();
}

} // namespace

void PmuA9::start() {
	// Stop every counter while the events change, and clear any overflow left from before.
	write_pmcntenclr(kCounterBits);
	write_pmovsr(kCounterBits);
	for (std::size_t i = 0; i < kEventCount; ++i) {
		write_pmselr(static_cast<uint32_t>(i));
		isb();
		write_pmxevtyper(kA9EventNumbers[i]);
	}
	write_pmcr((kPmcrEnable | kPmcrEventReset | kPmcrCycleReset) & ~kPmcrCycleDivider);
	write_pmcntenset(kCounterBits);
	isb();
}

Sample PmuA9::read() {
	// One read per counter, written out: under -fno-inline-functions an indexed std::array access is a call, which
	// would sit between the reads.
	static_assert(kEventCount == 5);
	isb();
	const uint32_t cycles = read_pmccntr();
	// A braced list evaluates in order, so the counters are read 0 to 4.
	return Sample{.cycles = cycles,
	              .events = {read_event(0), read_event(1), read_event(2), read_event(3), read_event(4)}};
}

} // namespace deluge::bench
