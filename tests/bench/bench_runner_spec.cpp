#include "bench/cases.h"
#include "bench/counters.h"
#include "bench/runner.h"
#include "dsp-next/fixed/dynamics/duck_envelope.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <cppspec.hpp>

using namespace deluge::bench;
namespace dsp = deluge::dsp;

namespace deluge::dsp::fixed {
/// A duck stage, printed by name, so a failing comparison shows it.
std::ostream& operator<<(std::ostream& out, DuckEnvelope::Stage stage) {
	switch (stage) {
	case DuckEnvelope::Stage::OFF:
		return out << "OFF";
	case DuckEnvelope::Stage::ATTACK:
		return out << "ATTACK";
	case DuckEnvelope::Stage::RELEASE:
		return out << "RELEASE";
	}
	return out;
}
} // namespace deluge::dsp::fixed

namespace {

/// Every line the runner emitted, in order.
std::vector<std::string> lines;

void capture(std::string_view line) {
	lines.emplace_back(line);
}

/// A counter bank whose first reading after each start() is 2^32 - 1 in every counter, so every timed delta that is
/// not zero wraps through zero.
///
/// Each read() costs `read_cost`, as a real counter read costs cycles; each fake block charges its own cost with
/// charge(). The runner's calibration therefore measures exactly one read_cost, which it must subtract.
class FakeCounters final : public Counters {
public:
	static constexpr Sample kReadCost{.cycles = 7, .events = {1, 2, 3, 4, 5}};

	Sample read_cost = kReadCost;
	std::size_t starts = 0;

	void reset() {
		read_cost = kReadCost;
		starts = 0;
		now_ = {};
		started_ = false;
	}

	void start() override {
		++starts;
		started_ = true;
	}

	[[nodiscard]] Sample read() override {
		if (started_) {
			started_ = false;
			now_.cycles = UINT32_MAX - read_cost.cycles;
			for (std::size_t i = 0; i < kEventCount; ++i) {
				now_.events[i] = UINT32_MAX - read_cost.events[i];
			}
		}
		charge(read_cost);
		return now_;
	}

	void charge(const Sample& cost) {
		now_.cycles += cost.cycles;
		for (std::size_t i = 0; i < kEventCount; ++i) {
			now_.events[i] += cost.events[i];
		}
	}

private:
	Sample now_{};
	bool started_ = false;
};

FakeCounters counters;

/// A fake component whose block costs a fixed amount, whatever its size.
struct FlatCost {
	std::size_t setups = 0;
	std::size_t blocks = 0;
	std::vector<std::size_t> setup_sizes;

	void setup(std::size_t n) {
		++setups;
		setup_sizes.push_back(n);
	}
	void block(std::size_t /*n*/) {
		++blocks;
		counters.charge({.cycles = 10, .events = {1, 0, 2, 0, 3}});
	}
};

/// A fake component whose block costs in proportion to its size, as a per-sample loop does.
struct PerSampleCost {
	std::size_t setups = 0;
	std::size_t blocks = 0;

	void setup(std::size_t /*n*/) { ++setups; }
	void block(std::size_t n) {
		++blocks;
		const auto w = static_cast<uint32_t>(n);
		counters.charge({.cycles = 3 * w, .events = {w, 2 * w, 0, 1, w}});
	}
};

/// A fake component that makes the counters cheaper to read than when the calibration measured them, so its own
/// delta falls below the calibration.
struct CheaperThanCalibration {
	void setup(std::size_t /*n*/) { counters.read_cost = {.cycles = 2, .events = {0, 0, 0, 0, 0}}; }
	void block(std::size_t /*n*/) {}
};

constexpr std::array<std::size_t, 3> kSizes{32, 64, 128};

std::string expected_line(std::string_view name, std::size_t n, std::size_t k, const Sample& delta) {
	std::array<char, kLineCapacity> buffer{};
	return std::string{format_line(buffer, name, n, k, delta)};
}

/// The case list's names, in order.
const std::vector<std::string_view> kCaseNames{
    "calibration_loop", "amplitude",     "crude_amplitude", "pulse",         "band_limited_table",
    "crude_saw",        "crude_square",  "crude_triangle",  "synced_pulse",  "wavetable",
    "mono_gain",        "mono_vec_gain", "stereo_gain",     "duck_envelope",
};

/// Renders `blocks` blocks of `n` through a case's own setup and block, as the runner does.
template <typename C>
void render(C& c, std::size_t n, std::size_t blocks) {
	c.setup(n);
	for (std::size_t i = 0; i < blocks; ++i) {
		c.block(n);
	}
}

} // namespace

// clang-format off
describe bench_runner("The DSP benchmark runner", ${
	before_each([] {
		lines.clear();
		counters.reset();
	});

	it("subtracts each counter modulo 2^32", _ {
		const Sample later{.cycles = 5, .events = {1, 0, 0xFFFF'FFFF, 4, 3}};
		const Sample earlier{.cycles = 0xFFFF'FFFE, .events = {0xFFFF'FFFF, 0, 0xFFFF'FFFE, 1, 3}};
		const Sample delta = later - earlier;
		expect(delta.cycles).to_equal(7u);
		expect(delta.events[0]).to_equal(2u);
		expect(delta.events[1]).to_equal(0u);
		expect(delta.events[2]).to_equal(1u);
		expect(delta.events[3]).to_equal(3u);
		expect(delta.events[4]).to_equal(0u);
	});

	it("formats a result line exactly", _ {
		std::array<char, kLineCapacity> buffer{};
		const Sample delta{.cycles = 1234, .events = {1, 2, 3, 4, 5}};
		expect(std::string{format_line(buffer, "osc.sine", 32, 64, delta)})
		    .to_equal(std::string{"BENCH case=osc.sine n=32 k=64 cycles=1234 cps=0.60 l1d_refill=1 inst=2 dc_stall=3 "
		                          "issue_empty=4 neon=5"});
	});

	it("prints the largest counts in full", _ {
		std::array<char, kLineCapacity> buffer{};
		const Sample delta{.cycles = 0xFFFF'FFFF,
		                   .events = {0xFFFF'FFFF, 0xFFFF'FFFF, 0xFFFF'FFFF, 0xFFFF'FFFF, 0xFFFF'FFFF}};
		expect(std::string{format_line(buffer, "max", 1, 1, delta)})
		    .to_equal(std::string{"BENCH case=max n=1 k=1 cycles=4294967295 cps=4294967295.00 l1d_refill=4294967295 "
		                          "inst=4294967295 dc_stall=4294967295 issue_empty=4294967295 neon=4294967295"});
	});

	it("rounds cycles per sample half up, to exactly two decimals", _ {
		std::array<char, kLineCapacity> buffer{};
		const auto cps = [&](uint32_t cycles, std::size_t n, std::size_t k) {
			const std::string line{format_line(buffer, "c", n, k, Sample{.cycles = cycles, .events = {}})};
			const auto from = line.find("cps=") + 4;
			return line.substr(from, line.find(' ', from) - from);
		};
		// 1234 / 2048 = 0.6025...: rounds down.
		expect(cps(1234, 32, 64)).to_equal(std::string{"0.60"});
		// 256 / 2048 = 0.125 exactly: a tie rounds up.
		expect(cps(256, 32, 64)).to_equal(std::string{"0.13"});
		// 2047 / 2048 = 0.9995...: the carry reaches the units.
		expect(cps(2047, 32, 64)).to_equal(std::string{"1.00"});
		expect(cps(0, 32, 64)).to_equal(std::string{"0.00"});
		expect(cps(4096 * 100 + 41, 64, 64)).to_equal(std::string{"100.01"});
		// A measurement of no samples has no rate.
		expect(cps(1234, 0, 64)).to_equal(std::string{"0.00"});
	});

	it("gives an empty line when the buffer cannot hold it", _ {
		std::array<char, 16> buffer{};
		const Sample delta{.cycles = 1234, .events = {1, 2, 3, 4, 5}};
		expect(format_line(buffer, "osc.sine", 32, 64, delta)).to_equal(std::string_view{});
	});

	it("sets each case up once per block size and renders warm-up plus timed blocks there", _ {
		FlatCost flat;
		PerSampleCost per_sample;
		const std::array cases{make_case("flat", flat), make_case("per_sample", per_sample)};
		const RunConfig config{.block_sizes = kSizes, .warmup_blocks = 4, .timed_blocks = 64};
		std::ignore = run(cases, config, counters, capture);

		expect(flat.setups).to_equal(kSizes.size());
		expect(per_sample.setups).to_equal(kSizes.size());
		expect(flat.blocks).to_equal(kSizes.size() * (4 + 64));
		expect(per_sample.blocks).to_equal(kSizes.size() * (4 + 64));
		expect(flat.setup_sizes).to_equal(std::vector<std::size_t>{32, 64, 128});
	});

	it("prints the calibration and each case at each size, then BENCH-DONE with the count", _ {
		FlatCost flat;
		PerSampleCost per_sample;
		const std::array cases{make_case("flat", flat), make_case("per_sample", per_sample)};
		const RunConfig config{.block_sizes = kSizes, .warmup_blocks = 4, .timed_blocks = 64};
		const std::size_t count = run(cases, config, counters, capture);

		expect(count).to_equal(std::size_t{9});
		expect(lines.size()).to_equal(std::size_t{10});
		expect(lines.back()).to_equal(std::string{"BENCH-DONE cases=9"});
		for (std::size_t size = 0; size < kSizes.size(); ++size) {
			expect(lines[size * 3]).to_start_with("BENCH case=calibration n=" + std::to_string(kSizes[size]) + " ");
			expect(lines[size * 3 + 1]).to_start_with("BENCH case=flat n=" + std::to_string(kSizes[size]) + " ");
			expect(lines[size * 3 + 2]).to_start_with("BENCH case=per_sample n=" + std::to_string(kSizes[size]) + " ");
		}
	});

	it("reports the calibration raw and each case less the calibration", _ {
		FlatCost flat;
		PerSampleCost per_sample;
		const std::array cases{make_case("flat", flat), make_case("per_sample", per_sample)};
		const RunConfig config{.block_sizes = kSizes, .warmup_blocks = 4, .timed_blocks = 64};
		std::ignore = run(cases, config, counters, capture);

		for (std::size_t size = 0; size < kSizes.size(); ++size) {
			const std::size_t n = kSizes[size];
			const auto w = static_cast<uint32_t>(n);
			// The calibration's timed loop holds one counter read and no work.
			expect(lines[size * 3]).to_equal(expected_line("calibration", n, 64, FakeCounters::kReadCost));
			expect(lines[size * 3 + 1])
			    .to_equal(expected_line("flat", n, 64, {.cycles = 64 * 10, .events = {64, 0, 128, 0, 192}}));
			expect(lines[size * 3 + 2])
			    .to_equal(expected_line("per_sample", n, 64,
			                            {.cycles = 64 * 3 * w, .events = {64 * w, 128 * w, 0, 64, 64 * w}}));
		}
		expect(lines[1]).to_equal(std::string{"BENCH case=flat n=32 k=64 cycles=640 cps=0.31 l1d_refill=64 inst=0 "
		                                      "dc_stall=128 issue_empty=0 neon=192"});
	});

	it("restarts the counters for every measurement", _ {
		FlatCost flat;
		const std::array cases{make_case("flat", flat)};
		const RunConfig config{.block_sizes = kSizes, .warmup_blocks = 4, .timed_blocks = 64};
		std::ignore = run(cases, config, counters, capture);
		expect(counters.starts).to_equal(kSizes.size() * 2);
	});

	it("reports zero, not a wrapped count, for a counter that measured under the calibration", _ {
		CheaperThanCalibration cheap;
		const std::array cases{make_case("cheap", cheap)};
		const std::array<std::size_t, 1> sizes{32};
		const RunConfig config{.block_sizes = sizes, .warmup_blocks = 4, .timed_blocks = 64};
		std::ignore = run(cases, config, counters, capture);
		expect(lines[1]).to_equal(expected_line("cheap", 32, 64, Sample{.cycles = 0, .events = {}}));
	});

	it("counts a line too long to format, so the capture reads as incomplete", _ {
		FlatCost flat;
		const std::string long_name(kLineCapacity, 'x');
		const std::array cases{make_case(long_name, flat)};
		const std::array<std::size_t, 1> sizes{32};
		const RunConfig config{.block_sizes = sizes, .warmup_blocks = 4, .timed_blocks = 64};
		const std::size_t count = run(cases, config, counters, capture);
		expect(count).to_equal(std::size_t{2});
		expect(lines[1]).to_equal(std::string{"BENCH-ERROR line-overflow"});
		expect(lines.back()).to_equal(std::string{"BENCH-DONE cases=2"});
	});

	context("the case list", _ {
		it("names every case in snake_case, with no space or '='", _ {
			std::vector<std::string_view> names;
			for (const Case& c : cases()) {
				names.push_back(c.name);
				expect(c.name.find_first_of(" =")).to_equal(std::string_view::npos);
				expect(c.name.size()).to_be_greater_than(std::size_t{0});
			}
			expect(names).to_equal(kCaseNames);
		});

		it("runs every case at every block size, the calibration's line included", _ {
			const RunConfig config{.block_sizes = kSizes, .warmup_blocks = 4, .timed_blocks = 64};
			const std::size_t count = run(cases(), config, counters, capture);

			const std::size_t per_size = cases().size() + 1;
			expect(count).to_equal(kSizes.size() * per_size);
			expect(lines.size()).to_equal(kSizes.size() * per_size + 1);
			expect(lines.back()).to_equal("BENCH-DONE cases=" + std::to_string(kSizes.size() * per_size));
			for (std::size_t size = 0; size < kSizes.size(); ++size) {
				const std::string n = std::to_string(kSizes[size]);
				expect(lines[size * per_size]).to_start_with("BENCH case=calibration n=" + n + " ");
				for (std::size_t i = 0; i < kCaseNames.size(); ++i) {
					expect(lines[size * per_size + 1 + i])
					    .to_start_with("BENCH case=" + std::string{kCaseNames[i]} + " n=" + n + " ");
				}
			}
		});

		it("fills the inputs from the same seed at every setup", _ {
			CaseSet& set = case_set();
			set.amplitude.setup(32);
			const std::vector<q31> first_a{input_a().begin(), input_a().end()};
			const std::vector<q31> first_b{input_b().begin(), input_b().end()};
			set.amplitude.setup(128);
			expect(std::vector<q31>{input_a().begin(), input_a().end()}).to_equal(first_a);
			expect(std::vector<q31>{input_b().begin(), input_b().end()}).to_equal(first_b);
			expect(first_a != first_b).to_be_truthy();
			expect(std::ranges::count(first_a, 0)).to_be_less_than(std::ptrdiff_t{2});
		});

		it("runs the calibration loop's known iteration count", _ {
			CaseSet& set = case_set();
			for (const std::size_t n : kSizes) {
				render(set.calibration_loop, n, 1);
				expect(set.calibration_loop.last_iterations())
				    .to_equal(static_cast<uint32_t>(CalibrationLoop::kIterationsPerSample * n));
			}
		});

		it("keeps each VCA's ramp mid-level and in range for a whole run", _ {
			for (const dsp::Ramp ramp : {AmplitudeCase::ramp(), CrudeAmplitudeCase::ramp()}) {
				const int64_t end = int64_t{ramp.value()}
				                    + (int64_t{ramp.increment()} * int64_t{kBlocksPerRun * kMaxBlock});
				expect(ramp.value()).to_be_greater_than(0);
				expect(ramp.increment()).to_be_greater_than(0);
				expect(end).to_be_less_than(int64_t{std::numeric_limits<int32_t>::max() / 2});
			}
		});

		it("runs each oscillator free at its configured increment", _ {
			CaseSet& set = case_set();
			for (const std::size_t n : kSizes) {
				const auto advanced = [n](uint32_t increment, std::size_t blocks) {
					return static_cast<uint32_t>(increment * n * blocks);
				};
				render(set.pulse, n, 3);
				expect(set.pulse.phase()).to_equal(advanced(PulseCase::kIncrement, 3));
				render(set.band_limited_table, n, 3);
				expect(set.band_limited_table.phase()).to_equal(advanced(BandLimitedTableCase::kIncrement, 3));
				render(set.crude_saw, n, 3);
				expect(set.crude_saw.phase()).to_equal(advanced(CrudeSawCase::kIncrement, 3));
				render(set.crude_square, n, 3);
				expect(set.crude_square.phase()).to_equal(advanced(CrudeSquareCase::kIncrement, 3));
				render(set.crude_triangle, n, 3);
				expect(set.crude_triangle.phase()).to_equal(advanced(CrudeTriangleCase::kIncrement, 3));
				render(set.wavetable, n, 3);
				expect(set.wavetable.phase()).to_equal(advanced(WavetableCase::kIncrement, 3));
			}
		});

		it("advances the synced pulse's master by each block and crosses over within every timed run", _ {
			CaseSet& set = case_set();
			for (const std::size_t n : kSizes) {
				render(set.synced_pulse, n, 3);
				expect(set.synced_pulse.master_phase())
				    .to_equal(static_cast<uint32_t>(SyncedPulseCase::kMasterIncrement * n * 3));
				// The timed blocks alone carry the master through at least one whole cycle.
				expect(uint64_t{SyncedPulseCase::kMasterIncrement} * n * 64).to_be_greater_than(uint64_t{1} << 32);
				expect(SyncedPulseCase::kAudibleIncrement).to_be_greater_than(SyncedPulseCase::kMasterIncrement);
			}
		});

		it("configures each gain as documented", _ {
			CaseSet& set = case_set();
			set.mono_gain.setup(32);
			expect(set.mono_gain.component().coefficients().gain).to_equal(MonoGainCase::kGain);
			set.mono_vec_gain.setup(32);
			expect(set.mono_vec_gain.component().coefficients().gain).to_equal(MonoVecGainCase::kGain);
			set.stereo_gain.setup(32);
			expect(set.stereo_gain.component().coefficients().gain).to_equal(StereoGainCase::kGain);
		});

		it("keeps the stereo frames at full scale through a whole in-place run", _ {
			CaseSet& set = case_set();
			set.stereo_gain.setup(kMaxBlock);
			const std::vector<StereoSample> before{stereo().begin(), stereo().end()};
			render(set.stereo_gain, kMaxBlock, kBlocksPerRun);
			for (std::size_t i = 0; i < kMaxBlock; ++i) {
				const int64_t drift_l = int64_t{before[i].l} - int64_t{stereo()[i].l};
				const int64_t drift_r = int64_t{before[i].r} - int64_t{stereo()[i].r};
				expect(drift_l).to_be_between(int64_t{0}, int64_t{2 * kBlocksPerRun + 1});
				expect(drift_r).to_be_between(int64_t{0}, int64_t{2 * kBlocksPerRun + 1});
			}
		});

		it("places the duck mid-release, and keeps it there, curved, for a whole run", _ {
			using Stage = dsp::fixed::DuckEnvelope::Stage;
			CaseSet& set = case_set();
			set.duck_envelope.setup(kMaxBlock);
			expect(set.duck_envelope.component().state().stage).to_equal(Stage::RELEASE);
			expect(set.duck_envelope.component().state().pos).to_equal(uint32_t{2'097'096});

			render(set.duck_envelope, kMaxBlock, kBlocksPerRun);
			const auto& duck = set.duck_envelope.component();
			expect(duck.state().stage).to_equal(Stage::RELEASE);
			expect(duck.state().pos).to_be_less_than(dsp::fixed::DuckEnvelope::kStageLength);
			expect(static_cast<int32_t>(duck.state().pos >> 7))
			    .to_be_less_than(duck.coefficients().curve_extent16);
		});
	});
});

CPPSPEC_SPEC(bench_runner)
