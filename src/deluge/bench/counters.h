#pragma once

/// @file
/// @brief The hardware counters a DSP benchmark reads, behind an interface the host spec can fake.

#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::bench {

/// @brief The events every case reports beside the cycle count, in the order a result line prints them.
enum class Event : uint8_t { L1D_REFILL, INSTRUCTIONS, DC_STALL, ISSUE_EMPTY, NEON };

/// @brief The number of events in Event.
inline constexpr std::size_t kEventCount = 5;

/// @brief One reading: the cycle counter and each event counter, raw.
///
/// Each counter is 32 bits wide and free-running, so only the difference of two readings means anything.
struct Sample {
	uint32_t cycles;                          ///< The cycle counter, in CPU cycles (no divider).
	std::array<uint32_t, kEventCount> events; ///< Each event's counter, indexed by Event.

	[[nodiscard]] constexpr bool operator==(const Sample&) const = default;
};

/// @brief The counts between two readings: `b - a`, each counter modulo 2^32.
///
/// Unsigned arithmetic wraps, so a counter that passed 2^32 between the readings still gives its true count, as long
/// as it advanced less than 2^32 in between.
/// @param b The later reading.
/// @param a The earlier reading.
/// @return Each counter's advance from `a` to `b`.
[[nodiscard]] constexpr Sample operator-(const Sample& b, const Sample& a) {
	Sample delta{.cycles = static_cast<uint32_t>(b.cycles - a.cycles), .events = {}};
	for (std::size_t i = 0; i < kEventCount; ++i) {
		delta.events[i] = static_cast<uint32_t>(b.events[i] - a.events[i]);
	}
	return delta;
}

/// @brief Reads the counters. The device implementation is the Cortex-A9 PMU; the host spec's is a fake.
class Counters {
public:
	virtual ~Counters() = default;

	/// @brief Enables the counters and zeroes them.
	virtual void start() = 0;

	/// @brief Reads every counter.
	/// @return The current reading.
	[[nodiscard]] virtual Sample read() = 0;
};

} // namespace deluge::bench
