#pragma once

/// @file
/// @brief The DSP benchmark runner: the measurement protocol every case is timed by, and the line format it prints.
///
/// **The protocol.** For each block size n, and for each case at that size:
///  1. the case's `setup` configures its component for n, fills its inputs and leaves it in the state it documents;
///  2. the counters are restarted, and the case renders `warmup_blocks` blocks untimed;
///  3. the counters are read, the case renders `timed_blocks` (K) consecutive blocks, and the counters are read again;
///  4. the difference of the two readings, modulo 2^32, is the case's raw delta;
///  5. the calibration's delta at the same n is subtracted from it, each counter saturating at zero;
///  6. the result is printed with cycles per sample, cycles ÷ (K·n).
///
/// **The calibration** is a case made by make_case() over an EmptyComponent, timed first at each block size through
/// the same loop, the same indirect call, the same trampoline into the component's `block` and the same two reads as
/// every other case. Its delta is the cost of the measurement itself, so
/// subtracting it leaves the cost of the component's blocks alone. It is printed raw, as `case=calibration`, so the
/// table shows how much was subtracted. A real case can measure under it by a few cycles of noise; that counter is
/// reported as zero rather than wrapping to near 2^32.
///
/// **The lines.** Each measurement prints one line, without a newline:
///
///     BENCH case=<name> n=<block> k=<blocks> cycles=<u64> cps=<x.xx> l1d_refill=<u32> inst=<u32> dc_stall=<u32>
///     issue_empty=<u32> neon=<u32>
///
/// (one line, wrapped here), and the run ends with `BENCH-DONE cases=<count>`, where count is the number of lines the
/// run printed before it, the calibration's included. A line too long for kLineCapacity (a case name of about 100
/// characters or more) is printed as `BENCH-ERROR line-overflow` and still counted, so a reader that compares the
/// count with the result lines it parsed sees the run as incomplete. Lines are formatted with std::to_chars into a
/// fixed buffer: the runner allocates nothing and does not use printf.
///
/// **cps** is cycles ÷ (K·n) with exactly two decimals, rounded half up: hundredths = ⌊(100·cycles + K·n/2) ÷ (K·n)⌋,
/// in 64-bit integers, so 0.125 prints as `0.13` and 0.9995 as `1.00`. A measurement of no samples prints `0.00`.
///
/// The runner neither masks interrupts nor programs the counters' events: its caller does both before calling run().

#include "bench/counters.h"

#include <concepts>
#include <cstddef>
#include <span>
#include <string_view>

namespace deluge::bench {

/// @brief Receives one complete line, without a newline.
using LineSink = void (*)(std::string_view line);

/// @brief The capacity of the buffer run() formats each line into, in characters.
inline constexpr std::size_t kLineCapacity = 256;

/// @brief A case: a name, and the functions that prepare and render one block of its component.
///
/// The component is reached through `context`, so the case table is an array of plain structs whose every call goes
/// through one indirect call, as the calibration's does; make_case() builds one from a typed component, so no caller
/// writes the cast.
struct Case {
	std::string_view name;                       ///< The case's name in its result line; no spaces.
	void* context;                               ///< The component the functions act on.
	void (*setup)(void* context, std::size_t n); ///< Configures for blocks of n, fills inputs, leaves it ready.
	void (*block)(void* context, std::size_t n); ///< Renders one block of n samples.
};

/// @brief A component a case can time: it prepares for a block size, then renders blocks of that size.
template <typename Component>
concept Benchable = requires(Component& component, std::size_t n) {
	{ component.setup(n) } -> std::same_as<void>;
	{ component.block(n) } -> std::same_as<void>;
};

/// @brief Builds a case that times `component`.
/// @param name The case's name in its result line; no spaces.
/// @param component The component; it must outlive every run() of the case.
/// @return A case whose `setup` and `block` call the component's own.
template <Benchable Component>
[[nodiscard]] constexpr Case make_case(std::string_view name, Component& component) {
	return Case{
	    .name = name,
	    .context = &component,
	    .setup = [](void* context, std::size_t n) { static_cast<Component*>(context)->setup(n); },
	    .block = [](void* context, std::size_t n) { static_cast<Component*>(context)->block(n); },
	};
}

/// @brief The calibration's component: a `setup` and a `block` that do nothing.
/// @details Both are defined out of line, as every case's are, so the calibration's trampoline makes the same call
/// a case's makes. Each body is an empty `asm volatile`, which emits no instruction but keeps the optimiser from
/// proving the call has no effect and removing it.
class EmptyComponent {
public:
	/// @brief Does nothing.
	void setup(std::size_t n);
	/// @brief Does nothing.
	void block(std::size_t n);
};

/// @brief The calibration case.
/// @return make_case() over an EmptyComponent, named `calibration`: the case run() times first at each block size.
[[nodiscard]] const Case& calibration();

/// @brief What run() measures: the block sizes, and how many blocks warm up and are timed at each.
struct RunConfig {
	std::span<const std::size_t> block_sizes; ///< The block sizes, in samples; the device runs {32, 64, 128}.
	std::size_t warmup_blocks = 4;            ///< Blocks rendered untimed before the timed ones.
	std::size_t timed_blocks = 64;            ///< Blocks timed together: K.
};

/// @brief Times every case at every block size by the protocol above, then prints the BENCH-DONE line.
/// @param cases The cases, in the order they print at each block size.
/// @param config The block sizes and block counts.
/// @param counters The counters to time with; their events are already selected.
/// @param sink Receives each line.
/// @return The number of lines printed before BENCH-DONE: the count it carries.
std::size_t run(std::span<const Case> cases, const RunConfig& config, Counters& counters, LineSink sink);

/// @brief Formats one result line.
/// @param buffer Where the line is written.
/// @param name The case's name.
/// @param n The block size, in samples.
/// @param k The number of blocks timed.
/// @param delta The counts to print, already less the calibration.
/// @return The line, a view into `buffer`; empty when `buffer` cannot hold it.
std::string_view format_line(std::span<char> buffer, std::string_view name, std::size_t n, std::size_t k,
                             const Sample& delta);

} // namespace deluge::bench
