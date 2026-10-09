#include "bench/runner.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <system_error>
#include <tuple>
#include <utility>

namespace deluge::bench {
namespace {

/// @brief Appends text to a fixed buffer, and remembers whether any of it did not fit.
class LineWriter {
public:
	explicit LineWriter(std::span<char> buffer) : buffer_{buffer} {}

	void text(std::string_view text) {
		if (overflowed_ || text.size() > buffer_.size() - used_) {
			overflowed_ = true;
			return;
		}
		text.copy(buffer_.data() + used_, text.size());
		used_ += text.size();
	}

	void number(uint64_t value) {
		if (overflowed_) {
			return;
		}
		const auto [end, error] = std::to_chars(buffer_.data() + used_, buffer_.data() + buffer_.size(), value);
		if (error != std::errc{}) {
			overflowed_ = true;
			return;
		}
		used_ = static_cast<std::size_t>(end - buffer_.data());
	}

	/// @brief A key=value field, preceded by the space that separates it from the one before.
	void field(std::string_view key, uint64_t value) {
		text(" ");
		text(key);
		text("=");
		number(value);
	}

	/// @brief Appends @p value as 0x and exactly eight lowercase hex digits.
	void hex32(uint32_t value) {
		constexpr std::size_t kDigits = 8;
		std::array<char, kDigits> digits{};
		const auto [end, error] = std::to_chars(digits.data(), digits.data() + kDigits, value, 16);
		const auto written = error == std::errc{} ? static_cast<std::size_t>(end - digits.data()) : kDigits;
		text("0x");
		for (std::size_t i = written; i < kDigits; ++i) {
			text("0");
		}
		text(std::string_view{digits.data(), written});
	}

	/// @return The text written, or an empty view if any of it did not fit.
	[[nodiscard]] std::string_view line() const {
		return overflowed_ ? std::string_view{} : std::string_view{buffer_.data(), used_};
	}

private:
	std::span<char> buffer_;
	std::size_t used_ = 0;
	bool overflowed_ = false;
};

/// @brief Hundredths of cycles per channel-sample, rounded half up: ⌊(100·cycles + K·n·N/2) ÷ (K·n·N)⌋, or 0 for no
/// samples.
[[nodiscard]] uint64_t cps_hundredths(uint32_t cycles, std::size_t n, std::size_t k, std::size_t lanes) {
	const uint64_t samples = uint64_t{n} * uint64_t{k} * uint64_t{lanes};
	if (samples == 0) {
		return 0;
	}
	return (uint64_t{cycles} * 100 + samples / 2) / samples;
}

/// @brief `measured - calibration`, each counter saturating at zero.
[[nodiscard]] Sample less_calibration(const Sample& measured, const Sample& calibration) {
	const auto less = [](uint32_t value, uint32_t subtrahend) -> uint32_t {
		return value > subtrahend ? value - subtrahend : 0;
	};
	Sample result{.cycles = less(measured.cycles, calibration.cycles), .events = {}};
	for (std::size_t i = 0; i < kEventCount; ++i) {
		result.events[i] = less(measured.events[i], calibration.events[i]);
	}
	return result;
}

/// @brief Steps 2 to 4 of the protocol: restart the counters, warm up, and time K blocks.
/// @return The raw delta of the timed blocks.
[[nodiscard]] Sample measure(const Case& c, std::size_t n, const RunConfig& config, Counters& counters) {
	counters.start();
	const std::size_t warmup_blocks = config.warmup_blocks(n);
	for (std::size_t i = 0; i < warmup_blocks; ++i) {
		c.block(c.context, n);
	}
	const Sample before = counters.read();
	for (std::size_t i = 0; i < config.timed_blocks; ++i) {
		c.block(c.context, n);
	}
	const Sample after = counters.read();
	return after - before;
}

/// @brief The calibration's component.
EmptyComponent empty_component;

/// @brief The calibration: the timing loop around an empty block, through the same trampoline as every case.
constinit const Case kCalibration = make_case("calibration", empty_component);

/// @brief Formats and prints one result line, or the overflow line in its place.
void emit(LineSink sink, std::string_view name, std::size_t n, std::size_t k, const Sample& delta, std::size_t lanes) {
	std::array<char, kLineCapacity> buffer{};
	const std::string_view line = format_line(buffer, name, n, k, delta, lanes);
	sink(line.empty() ? std::string_view{"BENCH-ERROR line-overflow"} : line);
}

} // namespace

void EmptyComponent::setup(std::size_t /*n*/) {
	asm volatile("");
}

void EmptyComponent::block(std::size_t /*n*/) {
	asm volatile("");
}

const Case& calibration() {
	return kCalibration;
}

std::string_view format_line(std::span<char> buffer, std::string_view name, std::size_t n, std::size_t k,
                             const Sample& delta, std::size_t lanes) {
	LineWriter out{buffer};
	out.text("BENCH case=");
	out.text(name);
	out.field("n", n);
	if (lanes > 1) {
		out.field("lanes", lanes);
	}
	out.field("k", k);
	out.field("cycles", delta.cycles);

	const uint64_t hundredths = cps_hundredths(delta.cycles, n, k, lanes);
	out.field("cps", hundredths / 100);
	out.text(".");
	const uint64_t fraction = hundredths % 100;
	if (fraction < 10) {
		out.text("0");
	}
	out.number(fraction);

	out.field("l1d_refill", delta.events[std::to_underlying(Event::L1D_REFILL)]);
	out.field("inst", delta.events[std::to_underlying(Event::INSTRUCTIONS)]);
	out.field("dc_stall", delta.events[std::to_underlying(Event::DC_STALL)]);
	out.field("issue_empty", delta.events[std::to_underlying(Event::ISSUE_EMPTY)]);
	out.field("neon", delta.events[std::to_underlying(Event::NEON)]);
	return out.line();
}

std::string_view format_info_line(std::span<char> buffer, std::string_view key, uint32_t value) {
	LineWriter out{buffer};
	out.text("BENCH-INFO ");
	out.text(key);
	out.text("=");
	out.hex32(value);
	return out.line();
}

std::size_t run(std::span<const Case> cases, const RunConfig& config, Counters& counters, LineSink sink) {
	std::size_t lines = 0;
	// The first measurement runs the counter reads and the timing loop for the first time, from cold caches; its
	// result is discarded, so the first block size's calibration is measured warm, as every later one is.
	if (!config.block_sizes.empty()) {
		const std::size_t first = config.block_sizes.front();
		kCalibration.setup(kCalibration.context, first);
		std::ignore = measure(kCalibration, first, config, counters);
	}
	for (const std::size_t n : config.block_sizes) {
		kCalibration.setup(kCalibration.context, n);
		const Sample calibration_delta = measure(kCalibration, n, config, counters);
		emit(sink, kCalibration.name, n, config.timed_blocks, calibration_delta, kCalibration.lanes);
		++lines;

		for (const Case& c : cases) {
			c.setup(c.context, n);
			const Sample delta = less_calibration(measure(c, n, config, counters), calibration_delta);
			emit(sink, c.name, n, config.timed_blocks, delta, c.lanes);
			++lines;
		}
	}

	std::array<char, 32> buffer{};
	LineWriter out{buffer};
	out.text("BENCH-DONE cases=");
	out.number(lines);
	sink(out.line());
	return lines;
}

} // namespace deluge::bench
