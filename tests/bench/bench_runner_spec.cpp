#include "bench/cases.h"
#include "bench/counters.h"
#include "bench/reference_impulse_response.h"
#include "bench/runner.h"
#include "dsp-next/core/chain.h"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/fixed/delay/delay_processor.h"
#include "dsp-next/fixed/delay/delay_tape.h"
#include "dsp-next/fixed/delay/feedback_shaper.h"
#include "dsp-next/fixed/dynamics/duck_envelope.h"
#include "dsp-next/fixed/filter/filter_set.h"
#include "dsp-next/fixed/filter/one_pole.h"
#include "dsp-next/fixed/filter/shift_tracker.h"
#include "dsp-next/fixed/mix/dry_wet.h"
#include "dsp-next/fixed/noise/congruential_step.h"
#include "dsp-next/fixed/shaping/legacy/antialiased_tanh.h"
#include "dsp-next/fixed/shaping/tanh.h"
#include "util/fixedpoint.h"
#include "util/waves.h" // jcong

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <limits>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
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

/// The shift tracker case's mode, printed by name, so a failing comparison shows it.
std::ostream& operator<<(std::ostream& out, ShiftTracker<11>::Mode mode) {
	switch (mode) {
	case ShiftTracker<11>::Mode::TRACK:
		return out << "TRACK";
	case ShiftTracker<11>::Mode::REMOVE:
		return out << "REMOVE";
	}
	return out;
}

/// A one-pole case's mode, printed by name, so a failing comparison shows it.
std::ostream& operator<<(std::ostream& out, OnePole::Mode mode) {
	switch (mode) {
	case OnePole::Mode::LOW_PASS:
		return out << "LOW_PASS";
	case OnePole::Mode::HIGH_PASS:
		return out << "HIGH_PASS";
	case OnePole::Mode::ALL_PASS:
		return out << "ALL_PASS";
	}
	return out;
}

/// The delay feedback cases' shaper mode, printed by name, so a failing comparison shows it.
std::ostream& operator<<(std::ostream& out, FeedbackShaper::Mode mode) {
	switch (mode) {
	case FeedbackShaper::Mode::ANALOG:
		return out << "ANALOG";
	case FeedbackShaper::Mode::DIGITAL:
		return out << "DIGITAL";
	}
	return out;
}

/// A dry/wet blend case's mode, printed by name, so a failing comparison shows it.
std::ostream& operator<<(std::ostream& out, DryWetBlend::Mode mode) {
	switch (mode) {
	case DryWetBlend::Mode::BLEND:
		return out << "BLEND";
	case DryWetBlend::Mode::FULL_WET:
		return out << "FULL_WET";
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
///
/// The second read since reset() also charges `cold_cost`, once: the first measurement's code runs from cold caches
/// on the device, so its first timed region costs more than every later one.
class FakeCounters final : public Counters {
public:
	static constexpr Sample kReadCost{.cycles = 7, .events = {1, 2, 3, 4, 5}};

	Sample read_cost = kReadCost;
	Sample cold_cost{};
	std::size_t starts = 0;

	void reset() {
		read_cost = kReadCost;
		cold_cost = {};
		starts = 0;
		reads_ = 0;
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
		if (++reads_ == 2) {
			charge(cold_cost);
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
	std::size_t reads_ = 0;
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

/// A fake component that records whether every block was rendered at the size it was last set up for. The synced
/// pulse and the duck take n in `block` as well as in `setup`, so the runner must pass both the same n.
struct SizeRecorder {
	std::size_t setup_n = 0;
	std::size_t blocks = 0;
	std::size_t mismatched_blocks = 0;
	std::vector<std::size_t> blocks_per_setup; ///< The blocks rendered after each setup.

	void setup(std::size_t n) {
		setup_n = n;
		blocks_per_setup.push_back(0);
	}
	void block(std::size_t n) {
		++blocks;
		++blocks_per_setup.back();
		mismatched_blocks += n == setup_n ? 0 : 1;
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

/// A component whose lane count is declared as an int rather than a std::size_t.
struct IntLanesComponent {
	static constexpr int kLanes = 2;
	void setup(std::size_t /*n*/) {}
	void block(std::size_t /*n*/) {}
};

static_assert(!Benchable<IntLanesComponent>, "a kLanes of another type than std::size_t is rejected, not read as 1");
static_assert(Benchable<CheaperThanCalibration>);

/// The lines the app prints over RTT before the run's own: a plain marker and the debug authentication line.
constexpr std::size_t kAppPreambleLines = 2;

std::string expected_line(std::string_view name, std::size_t n, std::size_t k, const Sample& delta) {
	std::array<char, kLineCapacity> buffer{};
	return std::string{format_line(buffer, name, n, k, delta)};
}

// The timed blocks alone carry the synced pulse's master through at least one whole cycle at the smallest block, and
// the audible pulse runs faster than the master, so every timed run crosses over.
static_assert(uint64_t{SyncedPulseCase::kMasterIncrement} * kSizes.front() * RunConfig{}.timed_blocks
              > (uint64_t{1} << 32));
static_assert(SyncedPulseCase::kAudibleIncrement > SyncedPulseCase::kMasterIncrement);

/// The kernels list's names, in order.
const std::vector<std::string_view> kKernelCaseNames{
    "calibration_loop", "amplitude",     "crude_amplitude",  "pulse",         "band_limited_table",
    "crude_saw",        "crude_square",  "crude_triangle",   "synced_pulse",  "wavetable",
    "mono_gain",        "mono_vec_gain", "stereo_gain",      "duck_envelope", "shift_tracker",
    "one_pole_lp",      "one_pole_ap",   "antialiased_tanh", "tanh3",         "dry_wet_blend",
};

/// The lanes list's names, in order.
const std::vector<std::string_view> kLaneCaseNames{
    "calibration_loop",        "one_pole_lp",      "one_pole_x2",      "one_pole_x4",
    "shift_tracker",           "shift_tracker_x4", "shift_tracker_x8", "antialiased_tanh",
    "stereo_antialiased_tanh", "mono_gain",        "stereo_gain",
};

/// Each lanes list case's lane count, in order.
const std::vector<std::size_t> kLaneCaseLanes{1, 1, 2, 4, 1, 4, 8, 1, 2, 1, 2};

/// The chain list's names, in order.
const std::vector<std::string_view> kChainCaseNames{
    "calibration_loop",
    "delay_feedback_legacy_analog",
    "delay_feedback_passes_analog",
    "delay_feedback_chain_analog",
    "delay_feedback_passes_digital",
    "delay_feedback_chain_digital",
    "delay_feedback_chain_digital_out_of_place",
    "tanh3",
    "one_pole_lp",
    "shift_tracker",
    "chain3_passes",
    "chain3_fused",
};

/// Each chain list case's lane count, in order: the delay's feedback cases are stereo.
const std::vector<std::size_t> kChainCaseLanes{1, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1};

/// The delay list's names, in order.
const std::vector<std::string_view> kDelayCaseNames{
    "calibration_loop",
    "delay_whole_native_straight_digital",
    "delay_whole_native_straight_analog",
    "delay_whole_native_cross_digital",
    "delay_whole_native_cross_analog",
    "delay_whole_res_lo_straight_digital",
    "delay_whole_res_lo_straight_analog",
    "delay_whole_res_lo_cross_digital",
    "delay_whole_res_lo_cross_analog",
    "delay_whole_res_hi_straight_digital",
    "delay_whole_res_hi_straight_analog",
    "delay_whole_res_hi_cross_digital",
    "delay_whole_res_hi_cross_analog",
    "delay_whole_growing",
    "delay_feedback_chain_analog",
    "delay_feedback_chain_digital",
};

/// The filters list's names, in order.
const std::vector<std::string_view> kFilterCaseNames{
    "calibration_loop",
    "filter_lp12_mono",
    "filter_lp12_stereo",
    "filter_lp24_mono",
    "filter_lp24_stereo",
    "filter_lpdrive_mono",
    "filter_lpdrive_stereo",
    "filter_lpdrive_os_mono",
    "filter_lpdrive_os_stereo",
    "filter_hp_mono",
    "filter_hp_stereo",
    "filter_svf_band_mono",
    "filter_svf_band_stereo",
    "filter_svf_notch_mono",
    "filter_svf_notch_stereo",
    "filterset_hl_stereo",
    "filterset_lh_stereo",
    "filterset_par_stereo",
    "delay_feedback_chain_analog",
    "delay_feedback_chain_digital",
};

/// Each filters list case's configuration and the noise draws it makes per channel-sample, in order after the
/// calibration loop: the low-pass ladder draws once per channel-sample, twice when the drive ladder oversamples, and
/// the high-pass ladder and the SVF never.
const std::vector<std::pair<FilterCaseConfig, uint32_t>> kFilterCaseConfigs{
    {kFilterLow12, 1}, {kFilterLow24, 1},   {kFilterLowDrive, 1}, {kFilterLowDriveOs, 2},
    {kFilterHigh, 0},  {kFilterSvfBand, 0}, {kFilterSvfNotch, 0},
};

/// Each filter set case's route, in order after the single filters.
const std::vector<dsp::fixed::FilterRoute> kFilterSetRoutes{
    dsp::fixed::FilterRoute::HIGH_TO_LOW,
    dsp::fixed::FilterRoute::LOW_TO_HIGH,
    dsp::fixed::FilterRoute::PARALLEL,
};

/// Each delay list case's configuration, in order after the calibration loop.
const std::vector<DelayWholeConfig> kDelayCaseConfigs{
    delay_whole_mode(DelayTapeForm::NATIVE, false, false),
    delay_whole_mode(DelayTapeForm::NATIVE, false, true),
    delay_whole_mode(DelayTapeForm::NATIVE, true, false),
    delay_whole_mode(DelayTapeForm::NATIVE, true, true),
    delay_whole_mode(DelayTapeForm::RES_LO, false, false),
    delay_whole_mode(DelayTapeForm::RES_LO, false, true),
    delay_whole_mode(DelayTapeForm::RES_LO, true, false),
    delay_whole_mode(DelayTapeForm::RES_LO, true, true),
    delay_whole_mode(DelayTapeForm::RES_HI, false, false),
    delay_whole_mode(DelayTapeForm::RES_HI, false, true),
    delay_whole_mode(DelayTapeForm::RES_HI, true, false),
    delay_whole_mode(DelayTapeForm::RES_HI, true, true),
    kDelayWholeGrowing,
};

/// A fake component of four lanes whose block costs a fixed amount per frame.
struct FourLanes {
	static constexpr std::size_t kLanes = 4;
	void setup(std::size_t /*n*/) {}
	void block(std::size_t n) { counters.charge({.cycles = 10 * static_cast<uint32_t>(n), .events = {}}); }
};

/// Lane @p lane of frame @p i of a lanes block of @p lanes lanes.
q31 lane_sample(std::span<const q31> block, std::size_t lanes, std::size_t i, std::size_t lane) {
	return block[(i * lanes) + lane];
}

/// Renders `blocks` blocks of `n` through a case's own setup and block, as the runner does.
template <typename C>
void render(C& c, std::size_t n, std::size_t blocks) {
	c.setup(n);
	for (std::size_t i = 0; i < blocks; ++i) {
		c.block(n);
	}
}

/// A case's component. CppSpec defines `context` as a macro, so the member is named with the macro set aside.
void* component_of(const Case& c) {
#pragma push_macro("context")
#undef context
	return c.context;
#pragma pop_macro("context")
}

/// The delay's high-pass: a one-pole low-pass, a 1/2048 step per sample, taken out of the input, wrapping as the
/// hardware's add and sub do. Steps @p memory and returns the high-passed sample.
q31 delay_high_pass(q31 x, q31& memory) {
	const auto step = static_cast<q31>(static_cast<uint32_t>(x) - static_cast<uint32_t>(memory)) >> 11;
	memory = static_cast<q31>(static_cast<uint32_t>(memory) + static_cast<uint32_t>(step));
	return static_cast<q31>(static_cast<uint32_t>(x) - static_cast<uint32_t>(memory));
}

/// The delay's feedback as it shipped before it ran as one chain: the stereo impulse response (analog only), the
/// feedback amount and its saturation, then the high-pass, each a loop of its own over the block, every add wrapping
/// as the hardware's does. @p blocks blocks of @p n frames from silence, from the lanes input's first @p n frames,
/// filled as the delay cases fill it, at the delay cases' feedback and drive: in place, each block after the first
/// renders the one before's output.
struct DelayFeedbackReference {
	std::vector<q31> last_block; ///< The last block's output, left and right interleaved.
	q31 memory_l = 0;            ///< The left high-pass memory after every block.
	q31 memory_r = 0;            ///< The right high-pass memory after every block.
};

DelayFeedbackReference delay_feedback_reference(std::size_t n, bool analog, std::size_t blocks, Placement placement) {
	fill_delay_input();
	reference::WrappingReferenceImpulseResponse response;
	DelayFeedbackReference out;
	std::vector<StereoSample> work(n);
	for (std::size_t block = 0; block < blocks; ++block) {
		if (block == 0 || placement == Placement::OUT_OF_PLACE) {
			for (std::size_t i = 0; i < n; ++i) {
				work[i] = {.l = lane_sample(lanes_input(), 2, i, 0), .r = lane_sample(lanes_input(), 2, i, 1)};
			}
		}
		if (analog) {
			for (StereoSample& sample : work) {
				sample = response.process(sample);
			}
			for (StereoSample& sample : work) {
				sample.l = dsp::fixed::tanh(multiply_32x32_rshift32(sample.l, kDelayFeedback), kDelayDrive) << 2;
				sample.r = dsp::fixed::tanh(multiply_32x32_rshift32(sample.r, kDelayFeedback), kDelayDrive) << 2;
			}
		}
		else {
			for (StereoSample& sample : work) {
				sample.l = signed_saturate<32 - 3>(multiply_32x32_rshift32(sample.l, kDelayFeedback)) << 2;
				sample.r = signed_saturate<32 - 3>(multiply_32x32_rshift32(sample.r, kDelayFeedback)) << 2;
			}
		}
		for (StereoSample& sample : work) {
			sample.l = delay_high_pass(sample.l, out.memory_l);
			sample.r = delay_high_pass(sample.r, out.memory_r);
		}
	}
	for (const StereoSample& sample : work) {
		out.last_block.push_back(sample.l);
		out.last_block.push_back(sample.r);
	}
	return out;
}

/// The lanes output's first @p n stereo frames, left and right interleaved.
std::vector<q31> stereo_output(std::size_t n) {
	return {lanes_output().begin(), lanes_output().begin() + static_cast<std::ptrdiff_t>(2 * n)};
}

/// @p frames as interleaved q31, so a comparison prints them.
std::vector<q31> interleaved(std::span<const StereoSample> frames) {
	std::vector<q31> out;
	out.reserve(2 * frames.size());
	for (const StereoSample frame : frames) {
		out.push_back(frame.l);
		out.push_back(frame.r);
	}
	return out;
}

/// A filter case's run, rendered by direct calls to a filter set of its own.
struct FilterReference {
	std::vector<q31> last_block; ///< The block after the run, mono or interleaved.
	q31 gain{};                  ///< The last configure's gain.
	uint32_t noise{};            ///< The synth-wide noise state after the run.
};

/// @p config's run of @p blocks blocks of @p n frames (one sample each, two in @p stereo), from the filter cases'
/// documented start: a reset set, the noise at kFilterNoiseSeed, the block the lanes input's first samples as
/// fill_filter_input() fills them, each block configured and filtered in place.
FilterReference filter_reference(const FilterCaseConfig& config, std::size_t n, bool stereo, std::size_t blocks) {
	const std::size_t samples = stereo ? 2 * n : n;
	fill_filter_input();
	FilterReference out;
	out.last_block.assign(lanes_input().begin(), lanes_input().begin() + static_cast<std::ptrdiff_t>(samples));
	std::vector<q31> fade(samples);
	std::vector<q31> parallel(samples);
	jcong = kFilterNoiseSeed;
	dsp::fixed::FilterSet set;
	for (std::size_t b = 0; b < blocks; ++b) {
		out.gain =
		    set.configure(kFilterGain, config.low_config, config.low, config.high_config, config.high, config.route);
		if (stereo) {
			set.renderStereo(out.last_block, {.fade = fade, .parallel = parallel});
		}
		else {
			set.renderMono(out.last_block, {.fade = fade, .parallel = parallel});
		}
	}
	out.noise = jcong;
	return out;
}

/// The noise state @p draws draws after @p seed.
uint32_t noise_after(uint32_t seed, std::size_t draws) {
	for (std::size_t i = 0; i < draws; ++i) {
		seed = dsp::fixed::CongruentialStep::next(seed);
	}
	return seed;
}

/// A delay whole case's run, rendered by a direct call to `DelayProcessor::process` per block over tapes of its own.
struct DelayWholeReference {
	std::vector<q31> last_block;                  ///< The last block, interleaved.
	std::vector<StereoSample> primary;            ///< The primary tape's storage after the run.
	std::vector<StereoSample> secondary;          ///< The secondary's, empty unless the case grows one.
	dsp::fixed::DelayTape::Head primary_head{};   ///< The primary's read head after the run.
	dsp::fixed::DelayTape::Head secondary_head{}; ///< The secondary's read head after the run, when it grows one.
	dsp::fixed::DelayBlockResult result{};        ///< The last block's result.
	std::vector<std::int64_t> feedback; ///< The processor's feedback memories after the run (delay_feedback_words).
};

/// @return @p processor's feedback memories from its kernel's State, left then right: each channel's impulse response
/// history and its oldest index, then its high-pass memory.
std::vector<std::int64_t> delay_feedback_words(const dsp::fixed::DelayProcessor& processor) {
	std::vector<std::int64_t> out;
	for (const auto& state : {processor.state().left, processor.state().right}) {
		const auto& response = dsp::get<0>(state).response;
		out.insert(out.end(), std::begin(response.history), std::end(response.history));
		out.push_back(response.oldest);
		out.push_back(dsp::get<1>(state).y);
	}
	return out;
}

/// @p config's run of @p blocks blocks of @p n frames, from the delay cases' documented start: the primary tape the
/// neutral rate's, filled by fill_delay_tape, native at kDelayNeutralRate; growing, a zeroed secondary for
/// kDelayResHiRate, native there, the primary's native rate made precise relative to it; the block a quarter of the
/// lanes input's first n frames, rendered in place.
DelayWholeReference delay_whole_reference(const DelayWholeConfig& config, std::size_t n, std::size_t blocks) {
	using dsp::fixed::DelayTape;
	constexpr auto kLag = static_cast<std::size_t>(DelayTape::kWriteLag);
	fill_delay_input();
	std::vector<StereoSample> work(n);
	for (std::size_t i = 0; i < n; ++i) {
		work[i] = {.l = lane_sample(lanes_input(), 2, i, 0) >> 1, .r = lane_sample(lanes_input(), 2, i, 1) >> 1};
	}
	DelayWholeReference out;
	out.primary.resize(DelayTape::kNeutralSize + kLag);
	fill_delay_tape(out.primary);
	DelayTape primary{out.primary, DelayTape::kNeutralSize, static_cast<uint32_t>(kDelayNeutralRate)};
	DelayTape secondary;
	if (config.growing) {
		const auto size = static_cast<std::size_t>(DelayTape::idealSizeForRate(kDelayResHiRate).first);
		out.secondary.assign(size + kLag, StereoSample{.l = 0, .r = 0});
		secondary = DelayTape{out.secondary, size, static_cast<uint32_t>(kDelayResHiRate)};
		primary.makeNativeRatePreciseRelativeTo(secondary);
	}

	dsp::fixed::DelayProcessor processor;
	for (std::size_t b = 0; b < blocks; ++b) {
		out.result =
		    processor.process(work, &primary, config.growing ? &secondary : nullptr, delay_whole_block(config));
	}
	out.last_block = interleaved(work);
	out.primary_head = primary.head();
	out.secondary_head = secondary.head();
	out.feedback = delay_feedback_words(processor);
	return out;
}

/// The chain3 cases' stages on full-scale input over two blocks of input_a's first @p n samples from silence: tanh at
/// drive 3, the one-pole low-pass at the granular moveability, then the delay's high-pass. Returns the second block,
/// and leaves each memory in @p low and @p high.
std::vector<q31> chain3_reference(std::size_t n, uint32_t& low, q31& high) {
	std::vector<q31> second(n);
	low = 0;
	high = 0;
	for (std::size_t block = 0; block < 2; ++block) {
		for (std::size_t i = 0; i < n; ++i) {
			const auto x = static_cast<uint32_t>(dsp::fixed::tanh<3>(input_a()[i]));
			const auto difference = static_cast<q31>(x - low);
			const auto step = static_cast<uint32_t>(multiply_32x32_rshift32_rounded(difference, 1 << 29) << 1);
			const uint32_t low_passed = step + low;
			low = low_passed + step;
			second[i] = delay_high_pass(static_cast<q31>(low_passed), high);
		}
	}
	return second;
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

	it("formats an info line with eight hex digits, zero-padded", _ {
		std::array<char, kLineCapacity> buffer{};
		expect(std::string{format_info_line(buffer, "dbgauthstatus", 0xAA)})
		    .to_equal(std::string{"BENCH-INFO dbgauthstatus=0x000000aa"});
		expect(std::string{format_info_line(buffer, "dbgauthstatus", 0)})
		    .to_equal(std::string{"BENCH-INFO dbgauthstatus=0x00000000"});
		expect(std::string{format_info_line(buffer, "x", 0xDEAD'BEEF)}).to_equal(std::string{"BENCH-INFO x=0xdeadbeef"});
	});

	it("returns an empty view for an info line that does not fit", _ {
		std::array<char, 16> buffer{};
		expect(format_info_line(buffer, "dbgauthstatus", 0xAA)).to_equal(std::string_view{});
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

	it("names a case's lanes and gives its cycles per channel-sample", _ {
		std::array<char, kLineCapacity> buffer{};
		const Sample delta{.cycles = 1234, .events = {1, 2, 3, 4, 5}};
		expect(std::string{format_line(buffer, "x4", 32, 64, delta, 4)})
		    .to_equal(std::string{"BENCH case=x4 n=32 lanes=4 k=64 cycles=1234 cps=0.15 l1d_refill=1 inst=2 dc_stall=3 "
		                          "issue_empty=4 neon=5"});
		// One lane is a mono case's line, with no lanes field.
		expect(std::string{format_line(buffer, "x1", 32, 64, delta, 1)})
		    .to_equal(std::string{format_line(buffer, "x1", 32, 64, delta)});
		// cps divides by K · n · lanes: 4096 / (64 · 32 · 2) = 1 exactly, and 4096 / (64 · 32 · 8) = 0.25.
		const auto cps = [&](uint32_t cycles, std::size_t lanes) {
			const std::string line{format_line(buffer, "c", 32, 64, Sample{.cycles = cycles, .events = {}}, lanes)};
			const auto from = line.find("cps=") + 4;
			return line.substr(from, line.find(' ', from) - from);
		};
		expect(cps(4096, 2)).to_equal(std::string{"1.00"});
		expect(cps(4096, 8)).to_equal(std::string{"0.25"});
		// 8274 / 16384 = 0.50500...: rounds half up, over the lanes' samples.
		expect(cps(8274, 8)).to_equal(std::string{"0.51"});
	});

	it("passes a case's lane count to its line", _ {
		FourLanes four;
		const std::array cases{make_case("four", four)};
		expect(cases[0].lanes).to_equal(std::size_t{4});
		const std::array<std::size_t, 1> sizes{32};
		const RunConfig config{.block_sizes = sizes};
		std::ignore = run(cases, config, counters, capture);
		// 10 cycles per frame over 64 blocks of 32 frames, of 4 lanes: 2.50 per channel-sample.
		expect(lines[1]).to_start_with(std::string{"BENCH case=four n=32 lanes=4 k=64 cycles=20480 cps=2.50 "});
		expect(lines[0]).to_start_with(std::string{"BENCH case=calibration n=32 k=64 "});
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
		const RunConfig config{.block_sizes = kSizes};
		std::ignore = run(cases, config, counters, capture);

		expect(flat.setups).to_equal(kSizes.size());
		expect(per_sample.setups).to_equal(kSizes.size());
		// 512 samples of warm-up: 16 blocks at 32, 8 at 64 and 4 at 128, each before 64 timed.
		expect(flat.blocks).to_equal(std::size_t{(16 + 64) + (8 + 64) + (4 + 64)});
		expect(per_sample.blocks).to_equal(std::size_t{(16 + 64) + (8 + 64) + (4 + 64)});
		expect(flat.setup_sizes).to_equal(std::vector<std::size_t>{32, 64, 128});
	});

	it("renders every block at the size its case was set up for", _ {
		SizeRecorder recorder;
		const std::array cases{make_case("recorder", recorder)};
		const RunConfig config{.block_sizes = kSizes};
		std::ignore = run(cases, config, counters, capture);

		expect(recorder.blocks).to_equal(std::size_t{(16 + 64) + (8 + 64) + (4 + 64)});
		expect(recorder.mismatched_blocks).to_equal(std::size_t{0});
	});

	it("warms every block size up by at least kWarmupSamples samples, and by at least four blocks", _ {
		SizeRecorder recorder;
		const std::array cases{make_case("recorder", recorder)};
		const std::array<std::size_t, 5> sizes{32, 64, 128, 256, 100};
		const RunConfig config{.block_sizes = sizes};
		std::ignore = run(cases, config, counters, capture);

		expect(kWarmupSamples).to_equal(std::size_t{512});
		std::vector<std::size_t> warmups;
		for (const std::size_t blocks : recorder.blocks_per_setup) {
			warmups.push_back(blocks - config.timed_blocks);
		}
		// 512 / 100 rounds up to 6 blocks; at 256, 2 blocks would cover 512 samples, but every size warms up by 4.
		expect(warmups).to_equal(std::vector<std::size_t>{16, 8, 4, 4, 6});
		for (std::size_t i = 0; i < sizes.size(); ++i) {
			expect(config.warmup_blocks(sizes[i])).to_equal(warmups[i]);
			expect(config.blocks_per_run(sizes[i])).to_equal(warmups[i] + config.timed_blocks);
		}
	});

	it("prints the calibration and each case at each size, then BENCH-DONE with the count", _ {
		FlatCost flat;
		PerSampleCost per_sample;
		const std::array cases{make_case("flat", flat), make_case("per_sample", per_sample)};
		const RunConfig config{.block_sizes = kSizes};
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
		const RunConfig config{.block_sizes = kSizes};
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

	it("calibrates the first block size warm, as every later one", _ {
		counters.cold_cost = {.cycles = 5000, .events = {50, 400, 60, 700, 80}};
		FlatCost flat;
		const std::array cases{make_case("flat", flat)};
		const RunConfig config{.block_sizes = kSizes};
		std::ignore = run(cases, config, counters, capture);

		expect(lines[0]).to_equal(expected_line("calibration", 32, 64, FakeCounters::kReadCost));
		expect(lines[2]).to_equal(expected_line("calibration", 64, 64, FakeCounters::kReadCost));
		expect(lines[1]).to_equal(expected_line("flat", 32, 64, {.cycles = 64 * 10, .events = {64, 0, 128, 0, 192}}));
	});

	it("times the calibration through the same trampoline as every case", _ {
		const Case& c = calibration();
		expect(c.name).to_equal(std::string_view{"calibration"});
		expect(component_of(c)).not_().to_be_null();
		// make_case's trampolines are one function per component type, so a case over any other EmptyComponent has
		// the calibration's very functions.
		EmptyComponent other;
		const Case same_type = make_case("other", other);
		expect(c.block).to_equal(same_type.block);
		expect(c.setup).to_equal(same_type.setup);
		FlatCost flat;
		expect(c.block).not_().to_equal(make_case("flat", flat).block);
	});

	it("restarts the counters for every measurement", _ {
		FlatCost flat;
		const std::array cases{make_case("flat", flat)};
		const RunConfig config{.block_sizes = kSizes};
		std::ignore = run(cases, config, counters, capture);
		// One more than two per size: the discarded calibration that warms the measurement's code.
		expect(counters.starts).to_equal(kSizes.size() * 2 + 1);
	});

	it("reports zero, not a wrapped count, for a counter that measured under the calibration", _ {
		CheaperThanCalibration cheap;
		const std::array cases{make_case("cheap", cheap)};
		const std::array<std::size_t, 1> sizes{32};
		const RunConfig config{.block_sizes = sizes};
		std::ignore = run(cases, config, counters, capture);
		expect(lines[1]).to_equal(expected_line("cheap", 32, 64, Sample{.cycles = 0, .events = {}}));
	});

	it("counts a line too long to format, so the capture reads as incomplete", _ {
		FlatCost flat;
		const std::string long_name(kLineCapacity, 'x');
		const std::array cases{make_case(long_name, flat)};
		const std::array<std::size_t, 1> sizes{32};
		const RunConfig config{.block_sizes = sizes};
		const std::size_t count = run(cases, config, counters, capture);
		expect(count).to_equal(std::size_t{2});
		expect(lines[1]).to_equal(std::string{"BENCH-ERROR line-overflow"});
		expect(lines.back()).to_equal(std::string{"BENCH-DONE cases=2"});
	});

	context("the case list", _ {
		it("names every case in snake_case, with no space or '='", _ {
			for (const auto& [list, expected] : {std::pair{CaseList::KERNELS, kKernelCaseNames},
			                                     std::pair{CaseList::LANES, kLaneCaseNames},
			                                     std::pair{CaseList::CHAIN, kChainCaseNames},
			                                     std::pair{CaseList::DELAY, kDelayCaseNames},
			                                     std::pair{CaseList::FILTERS, kFilterCaseNames}}) {
				std::vector<std::string_view> names;
				for (const Case& c : cases(list)) {
					names.push_back(c.name);
					expect(c.name.find_first_of(" =")).to_equal(std::string_view::npos);
					expect(c.name.size()).to_be_greater_than(std::size_t{0});
				}
				expect(names).to_equal(expected);
			}
		});

		it("gives each lanes case its lane count, and the stereo gain two", _ {
			std::vector<std::size_t> lanes;
			for (const Case& c : cases(CaseList::LANES)) {
				lanes.push_back(c.lanes);
			}
			expect(lanes).to_equal(kLaneCaseLanes);
			lanes.clear();
			for (const Case& c : cases(CaseList::CHAIN)) {
				lanes.push_back(c.lanes);
			}
			expect(lanes).to_equal(kChainCaseLanes);
			for (const Case& c : cases(CaseList::DELAY)) {
				expect(c.lanes).to_equal(c.name == "calibration_loop" ? std::size_t{1} : std::size_t{2});
			}
			for (const Case& c : cases(CaseList::FILTERS)) {
				const bool mono = c.name == "calibration_loop" || c.name.ends_with("_mono");
				expect(c.lanes).to_equal(mono ? std::size_t{1} : std::size_t{2});
			}
			for (const Case& c : cases(CaseList::KERNELS)) {
				expect(c.lanes).to_equal(c.name == "stereo_gain" ? std::size_t{2} : std::size_t{1});
			}
		});

		it("runs every case at every device block size, the calibration's line included, in fewer than 70 lines", _ {
			for (const auto& [list, names] : {std::pair{CaseList::KERNELS, kKernelCaseNames},
			                                  std::pair{CaseList::LANES, kLaneCaseNames},
			                                  std::pair{CaseList::CHAIN, kChainCaseNames},
			                                  std::pair{CaseList::DELAY, kDelayCaseNames},
			                                  std::pair{CaseList::FILTERS, kFilterCaseNames}}) {
				lines.clear();
				counters.reset();
				const RunConfig config{.block_sizes = kBlockSizes};
				const std::size_t count = run(cases(list), config, counters, capture);

				const std::size_t per_size = cases(list).size() + 1;
				expect(count).to_equal(kBlockSizes.size() * per_size);
				expect(lines.size()).to_equal(kBlockSizes.size() * per_size + 1);
				expect(kAppPreambleLines + lines.size()).to_be_less_than(std::size_t{70});
				expect(lines.back()).to_equal("BENCH-DONE cases=" + std::to_string(kBlockSizes.size() * per_size));
				for (std::size_t size = 0; size < kBlockSizes.size(); ++size) {
					const std::string n = std::to_string(kBlockSizes[size]);
					expect(lines[size * per_size]).to_start_with("BENCH case=calibration n=" + n + " ");
					for (std::size_t i = 0; i < names.size(); ++i) {
						const std::size_t lanes = cases(list)[i].lanes;
						const std::string lanes_field = lanes > 1 ? " lanes=" + std::to_string(lanes) : "";
						expect(lines[size * per_size + 1 + i])
						    .to_start_with("BENCH case=" + std::string{names[i]} + " n=" + n + lanes_field + " k=");
					}
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
			expect(first_a).not_().to_equal(first_b);
			expect(std::ranges::count(first_a, 0)).to_be_less_than(std::ptrdiff_t{2});
		});

		it("runs the calibration loop's known iteration count", _ {
			CaseSet& set = case_set();
			for (const std::size_t n : kSizes) {
				render(set.calibration_loop, n, 1);
				expect(set.calibration_loop.last_iterations())
				    .to_equal(static_cast<uint32_t>(CalibrationLoop::kIterationsPerSample * n));
				// Each iteration multiplies the product by the factor twice, each multiply consuming the last one's result.
				uint32_t product = 1;
				for (std::size_t i = 0; i < CalibrationLoop::kIterationsPerSample * n * 2; ++i) {
					product *= CalibrationLoop::kProductFactor;
				}
				expect(set.calibration_loop.last_product()).to_equal(product);
			}
		});

		it("mixes the raw wave into the mix at the VCA's ramp", _ {
			CaseSet& set = case_set();
			// Amplitude: vqdmulh of the raw sample by the first lane's halved amplitude, (value + increment) >> 1, plus
			// the mix, wrapping.
			render(set.amplitude, kMaxBlock, 1);
			const int64_t half_amplitude = (int64_t{AmplitudeCase::kRamp.value()} + AmplitudeCase::kRamp.increment()) >> 1;
			const int64_t scaled = (2 * half_amplitude * int64_t{input_a()[0]}) >> 32;
			expect(output()[0]).to_equal(static_cast<q31>(scaled + int64_t{input_b()[0]}));

			// CrudeAmplitude: the amplitude steps first, then a rounded 32x32 high multiply-accumulate.
			render(set.crude_amplitude, kMaxBlock, 1);
			const int64_t amplitude = int64_t{CrudeAmplitudeCase::kRamp.value()} + CrudeAmplitudeCase::kRamp.increment();
			const int64_t product = ((amplitude * int64_t{input_a()[0]}) + (int64_t{1} << 31)) >> 32;
			expect(output()[0]).to_equal(static_cast<q31>(product + int64_t{input_b()[0]}));
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
			}
		});

		it("restarts the synced pulse at a sync reset within a run", _ {
			// Free-running, the pulse's half-rate phase advances by n times its increment each block, and is stored
			// back doubled. A reset at a crossover moves it off that track.
			CaseSet& set = case_set();
			for (const std::size_t n : kSizes) {
				const std::size_t blocks = RunConfig{}.blocks_per_run(n);
				uint32_t free_running = 0;
				for (std::size_t i = 0; i < blocks; ++i) {
					free_running = ((free_running >> 1)
					                + (static_cast<uint32_t>(n) * (SyncedPulseCase::kAudibleIncrement >> 1)))
					               << 1;
				}
				render(set.synced_pulse, n, blocks);
				expect(set.synced_pulse.audible_phase()).not_().to_equal(free_running);
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

		it("keeps the stereo frames at full scale through a whole in-place run at every block size", _ {
			CaseSet& set = case_set();
			for (const std::size_t n : kSizes) {
				const std::size_t blocks = RunConfig{}.blocks_per_run(n);
				set.stereo_gain.setup(n);
				const std::vector<StereoSample> before{stereo().begin(), stereo().end()};
				render(set.stereo_gain, n, blocks);
				for (std::size_t i = 0; i < n; ++i) {
					const int64_t drift_l = int64_t{before[i].l} - int64_t{stereo()[i].l};
					const int64_t drift_r = int64_t{before[i].r} - int64_t{stereo()[i].r};
					expect(drift_l).to_be_between(int64_t{0}, int64_t{2 * blocks + 1});
					expect(drift_r).to_be_between(int64_t{0}, int64_t{2 * blocks + 1});
				}
			}
		});

		it("removes input_a's slow part from silence, as the delay's blocker does, at every block size", _ {
			using Tracker = ShiftTrackerCase::Tracker;
			CaseSet& set = case_set();
			for (const std::size_t n : kSizes) {
				set.shift_tracker.setup(n);
				expect(set.shift_tracker.component().coefficients().mode).to_equal(Tracker::Mode::REMOVE);
				expect(set.shift_tracker.component().state().y).to_equal(q31{0});

				render(set.shift_tracker, n, 2);
				q31 hp = 0;
				for (std::size_t block = 0; block < 2; ++block) {
					for (std::size_t i = 0; i < n; ++i) {
						// The delay's blocker on full-scale input, wrapping as the hardware's add and sub do.
						const auto x = static_cast<uint32_t>(input_a()[i]);
						const auto step = static_cast<q31>(x - static_cast<uint32_t>(hp)) >> 11;
						hp = static_cast<q31>(static_cast<uint32_t>(hp) + static_cast<uint32_t>(step));
						if (block == 1) {
							expect(output()[i]).to_equal(static_cast<q31>(x - static_cast<uint32_t>(hp)));
						}
					}
				}
				expect(set.shift_tracker.component().state().y).to_equal(hp);
			}
		});

		it("low- and all-passes input_a from silence, as the one-pole class does, at every block size", _ {
			using Mode = dsp::fixed::OnePole::Mode;
			CaseSet& set = case_set();
			for (const std::size_t n : kSizes) {
				set.one_pole_lp.setup(n);
				set.one_pole_ap.setup(n);
				for (const auto* mono : {&set.one_pole_lp.component(), &set.one_pole_ap.component()}) {
					expect(mono->coefficients().m).to_equal(q31{1 << 29});
					expect(mono->state().z).to_equal(q31{0});
				}
				expect(set.one_pole_lp.component().coefficients().mode).to_equal(Mode::LOW_PASS);
				expect(set.one_pole_ap.component().coefficients().mode).to_equal(Mode::ALL_PASS);

				// The class's lowPass and allPass on full-scale input, wrapping as the hardware's add and sub do.
				const auto check = [&](auto& one_pole_case, bool all_pass) {
					render(one_pole_case, n, 2);
					uint32_t memory = 0;
					for (std::size_t block = 0; block < 2; ++block) {
						for (std::size_t i = 0; i < n; ++i) {
							const auto x = static_cast<uint32_t>(input_a()[i]);
							const auto difference = static_cast<q31>(x - memory);
							const auto step =
							    static_cast<uint32_t>(multiply_32x32_rshift32_rounded(difference, 1 << 29) << 1);
							const uint32_t low = step + memory;
							memory = low + step;
							if (block == 1) {
								expect(output()[i]).to_equal(static_cast<q31>(all_pass ? (low * 2) - x : low));
							}
						}
					}
					expect(one_pole_case.component().state().z).to_equal(static_cast<q31>(memory));
				};
				check(set.one_pole_lp, false);
				check(set.one_pole_ap, true);
			}
		});

		it("saturates input_a from the centre, as the anti-aliased tanh class does, at every block size", _ {
			CaseSet& set = case_set();
			for (const std::size_t n : kSizes) {
				set.antialiased_tanh.setup(n);
				expect(set.antialiased_tanh.component().coefficients().drive).to_equal(3u);
				expect(set.antialiased_tanh.component().state().position).to_equal(dsp::fixed::kTanhCentre);

				render(set.antialiased_tanh, n, 2);
				dsp::fixed::legacy::AntialiasedTanh curve{3};
				for (std::size_t block = 0; block < 2; ++block) {
					for (std::size_t i = 0; i < n; ++i) {
						const q31 expected = curve.render(input_a()[i]);
						if (block == 1) {
							expect(output()[i]).to_equal(expected);
						}
					}
				}
				expect(set.antialiased_tanh.component().state().position).to_equal(curve.previous());
			}
		});

		it("saturates input_a as tanh<3> does, at every block size", _ {
			CaseSet& set = case_set();
			for (const std::size_t n : kSizes) {
				set.tanh3.setup(n);
				render(set.tanh3, n, 2);
				for (std::size_t i = 0; i < n; ++i) {
					expect(output()[i]).to_equal(dsp::fixed::tanh<3>(input_a()[i]));
				}
			}
		});

		it("blends input_a (dry) with input_b (wet) at a half, as the filters' fade does, at every block size", _ {
			CaseSet& set = case_set();
			for (const std::size_t n : kSizes) {
				set.dry_wet_blend.setup(n);
				expect(set.dry_wet_blend.component().coefficients().level).to_equal(q31{1 << 30});
				expect(set.dry_wet_blend.component().coefficients().mode).to_equal(dsp::fixed::DryWetBlend::Mode::BLEND);
				render(set.dry_wet_blend, n, 2);
				for (std::size_t i = 0; i < n; ++i) {
					// The fade's arithmetic, at a wet level of a half.
					const q31 wet = multiply_32x32_rshift32(input_b()[i], 1 << 30);
					const q31 expected =
					    multiply_accumulate_32x32_rshift32_rounded(wet, input_a()[i], ONE_Q31 - (1 << 30)) << 1;
					expect(output()[i]).to_equal(expected);
				}
			}
		});

		it("low-passes each lane of the lanes input from silence, op-major, as the mono pole does, at every block size", _ {
			CaseSet& set = case_set();
			const auto check = [&]<std::size_t N>(LanesOnePoleCase<N>& c) {
				for (const std::size_t n : kSizes) {
					c.setup(n);
					expect(c.component().coefficients().m).to_equal(q31{1 << 29});
					expect(c.component().coefficients().mode).to_equal(dsp::fixed::OnePole::Mode::LOW_PASS);
					render(c, n, 2);
					for (std::size_t lane = 0; lane < N; ++lane) {
						expect(c.component().state(lane).z).not_().to_equal(q31{0});
						// The class's lowPass on lane `lane`'s stream, wrapping as the hardware's add and sub do.
						uint32_t memory = 0;
						for (std::size_t block = 0; block < 2; ++block) {
							for (std::size_t i = 0; i < n; ++i) {
								const auto x = static_cast<uint32_t>(lane_sample(lanes_input(), N, i, lane));
								const auto difference = static_cast<q31>(x - memory);
								const auto step =
								    static_cast<uint32_t>(multiply_32x32_rshift32_rounded(difference, 1 << 29) << 1);
								const uint32_t low = step + memory;
								memory = low + step;
								if (block == 1) {
									expect(lane_sample(lanes_output(), N, i, lane)).to_equal(static_cast<q31>(low));
								}
							}
						}
						expect(c.component().state(lane).z).to_equal(static_cast<q31>(memory));
					}
				}
			};
			check(set.one_pole_x2);
			check(set.one_pole_x4);
		});

		it("removes each lane's slow part from silence, op-major, as the delay's blocker does, at every block size", _ {
			CaseSet& set = case_set();
			const auto check = [&]<std::size_t N>(LanesShiftTrackerCase<N>& c) {
				for (const std::size_t n : kSizes) {
					c.setup(n);
					expect(c.component().coefficients().mode).to_equal(ShiftTrackerCase::Tracker::Mode::REMOVE);
					render(c, n, 2);
					for (std::size_t lane = 0; lane < N; ++lane) {
						q31 hp = 0;
						for (std::size_t block = 0; block < 2; ++block) {
							for (std::size_t i = 0; i < n; ++i) {
								const auto x = static_cast<uint32_t>(lane_sample(lanes_input(), N, i, lane));
								const auto step = static_cast<q31>(x - static_cast<uint32_t>(hp)) >> 11;
								hp = static_cast<q31>(static_cast<uint32_t>(hp) + static_cast<uint32_t>(step));
								if (block == 1) {
									expect(lane_sample(lanes_output(), N, i, lane))
									    .to_equal(static_cast<q31>(x - static_cast<uint32_t>(hp)));
								}
							}
						}
						expect(c.component().state(lane).y).to_equal(hp);
					}
				}
			};
			check(set.shift_tracker_x4);
			check(set.shift_tracker_x8);
		});

		it("saturates both lanes input channels from the centre, as the anti-aliased tanh class does, at every size", _ {
			CaseSet& set = case_set();
			for (const std::size_t n : kSizes) {
				set.stereo_antialiased_tanh.setup(n);
				const auto& stereo = set.stereo_antialiased_tanh.component();
				expect(stereo.coefficients().drive).to_equal(3u);
				expect(stereo.left().position).to_equal(dsp::fixed::kTanhCentre);
				expect(stereo.right().position).to_equal(dsp::fixed::kTanhCentre);
				render(set.stereo_antialiased_tanh, n, 2);
				for (std::size_t lane = 0; lane < 2; ++lane) {
					dsp::fixed::legacy::AntialiasedTanh curve{3};
					for (std::size_t block = 0; block < 2; ++block) {
						for (std::size_t i = 0; i < n; ++i) {
							const q31 expected = curve.render(lane_sample(lanes_input(), 2, i, lane));
							if (block == 1) {
								expect(lane_sample(lanes_output(), 2, i, lane)).to_equal(expected);
							}
						}
					}
					expect((lane == 0 ? stereo.left() : stereo.right()).position).to_equal(curve.previous());
				}
			}
		});

		it("places the duck mid-release, and keeps it there, curved, for a whole run at every block size", _ {
			using Stage = dsp::fixed::DuckEnvelope::Stage;
			CaseSet& set = case_set();
			expect(kSamplesPerRun).to_equal(std::size_t{8'704});
			for (const std::size_t n : kSizes) {
				set.duck_envelope.setup(n);
				expect(set.duck_envelope.component().state().stage).to_equal(Stage::RELEASE);
				expect(set.duck_envelope.component().state().pos).to_equal(uint32_t{2'097'096});

				render(set.duck_envelope, n, RunConfig{}.blocks_per_run(n));
				const auto& duck = set.duck_envelope.component();
				expect(duck.state().stage).to_equal(Stage::RELEASE);
				expect(duck.state().pos).to_be_less_than(dsp::fixed::DuckEnvelope::kStageLength);
				expect(static_cast<int32_t>(duck.state().pos >> 7))
				    .to_be_less_than(duck.coefficients().curve_extent16);
			}
		});

		it("shapes and high-passes the delay's feedback in place, as the delay renders it, in passes and as one chain, at every size", _ {
			CaseSet& set = case_set();
			for (const std::size_t n : kSizes) {
				for (const bool analog : {true, false}) {
					const DelayFeedbackReference expected = delay_feedback_reference(n, analog, 2, Placement::IN_PLACE);
					expect(std::ranges::count(expected.last_block, 0)).to_be_less_than(std::ptrdiff_t{2});
					// In place, the second block's input is the first's output, not the lanes input again.
					expect(expected.last_block)
					    .not_()
					    .to_equal(delay_feedback_reference(n, analog, 2, Placement::OUT_OF_PLACE).last_block);

					if (analog) {
						render(set.delay_feedback_passes_analog, n, 2);
						expect(stereo_output(n)).to_equal(expected.last_block);
						const auto& high_pass = set.delay_feedback_passes_analog.high_pass();
						expect(high_pass.left().y).to_equal(expected.memory_l);
						expect(high_pass.right().y).to_equal(expected.memory_r);

						render(set.delay_feedback_chain_analog, n, 2);
						expect(stereo_output(n)).to_equal(expected.last_block);
						const auto& chain = set.delay_feedback_chain_analog.component();
						expect(dsp::get<1>(chain.left()).y).to_equal(expected.memory_l);
						expect(dsp::get<1>(chain.right()).y).to_equal(expected.memory_r);
						expect(dsp::get<0>(chain.coefficients().mode)).to_equal(dsp::fixed::FeedbackShaper::Mode::ANALOG);
					}
					else {
						render(set.delay_feedback_passes_digital, n, 2);
						expect(stereo_output(n)).to_equal(expected.last_block);
						const auto& high_pass = set.delay_feedback_passes_digital.high_pass();
						expect(high_pass.left().y).to_equal(expected.memory_l);
						expect(high_pass.right().y).to_equal(expected.memory_r);

						render(set.delay_feedback_chain_digital, n, 2);
						expect(stereo_output(n)).to_equal(expected.last_block);
						const auto& chain = set.delay_feedback_chain_digital.component();
						expect(dsp::get<1>(chain.left()).y).to_equal(expected.memory_l);
						expect(dsp::get<1>(chain.right()).y).to_equal(expected.memory_r);
						expect(dsp::get<0>(chain.coefficients().mode)).to_equal(dsp::fixed::FeedbackShaper::Mode::DIGITAL);
						// A digital block holds the response still: its history stays silent, its index at the start.
						const auto& response = dsp::get<0>(chain.left()).response;
						expect(std::ranges::count(response.history, 0))
						    .to_equal(static_cast<std::ptrdiff_t>(std::size(response.history)));
						expect(response.oldest).to_equal(uint32_t{0});
					}
				}
			}
		});

		it("runs the digital chain out of place too, each block from the lanes input, at every size", _ {
			CaseSet& set = case_set();
			for (const std::size_t n : kSizes) {
				const DelayFeedbackReference expected = delay_feedback_reference(n, false, 2, Placement::OUT_OF_PLACE);
				render(set.delay_feedback_chain_digital_out_of_place, n, 2);
				expect(stereo_output(n)).to_equal(expected.last_block);
				const auto& chain = set.delay_feedback_chain_digital_out_of_place.component();
				expect(dsp::get<1>(chain.left()).y).to_equal(expected.memory_l);
				expect(dsp::get<1>(chain.right()).y).to_equal(expected.memory_r);
				expect(dsp::get<0>(chain.coefficients().mode)).to_equal(dsp::fixed::FeedbackShaper::Mode::DIGITAL);
			}
		});

		it("runs the delay's analog feedback as it shipped, in place, bit for bit the chain's output, at every block size", _ {
			CaseSet& set = case_set();
			for (const std::size_t n : kSizes) {
				const std::size_t blocks = RunConfig{}.blocks_per_run(n);
				const DelayFeedbackReference expected = delay_feedback_reference(n, true, blocks, Placement::IN_PLACE);
				render(set.delay_feedback_chain_analog, n, blocks);
				expect(stereo_output(n)).to_equal(expected.last_block);
				const auto& chain = set.delay_feedback_chain_analog.component();

				render(set.delay_feedback_legacy_analog, n, blocks);
				expect(stereo_output(n)).to_equal(expected.last_block);
				const auto& legacy = set.delay_feedback_legacy_analog;
				expect(legacy.high_pass_l()).to_equal(dsp::get<1>(chain.left()).y);
				expect(legacy.high_pass_r()).to_equal(dsp::get<1>(chain.right()).y);
				expect(legacy.high_pass_l()).to_equal(expected.memory_l);
				expect(legacy.high_pass_r()).to_equal(expected.memory_r);
				expect(legacy.drive()).to_equal(kDelayDrive);
				expect(legacy.feedback()).to_equal(kDelayFeedback);
			}
		});

		it("renders each delay whole case as a direct call to the delay's block per block, tapes and all, at every size", _ {
			using dsp::fixed::DelayTape;
			const std::span<const Case> delay = cases(CaseList::DELAY);
			// The calibration loop, the whole cases, then the two feedback chain controls.
			expect(delay.size()).to_equal(kDelayCaseConfigs.size() + 3);
			for (std::size_t i = 0; i < kDelayCaseConfigs.size(); ++i) {
				const DelayWholeConfig& config = kDelayCaseConfigs[i];
				const Case& c = delay[i + 1];
				for (const std::size_t n : kSizes) {
					const std::size_t blocks = RunConfig{}.blocks_per_run(n);
					const DelayWholeReference expected = delay_whole_reference(config, n, blocks);
					c.setup(component_of(c), n);
					for (std::size_t b = 0; b < blocks; ++b) {
						c.block(component_of(c), n);
					}
					const DelayRig& rig = delay_rig();

					expect(stereo_output(n)).to_equal(expected.last_block);
					expect(std::ranges::count(expected.last_block, 0)).to_be_less_than(std::ptrdiff_t{2});
					expect(interleaved({rig.primary.begin(), rig.primary.end()})).to_equal(interleaved(expected.primary));
					expect(rig.primary.head().pos - rig.primary.begin())
					    .to_equal(expected.primary_head.pos - expected.primary.data());
					expect(rig.primary.head().long_pos).to_equal(expected.primary_head.long_pos);
					expect(rig.primary.head().last_short_pos).to_equal(expected.primary_head.last_short_pos);
					expect(delay_feedback_words(rig.processor)).to_equal(expected.feedback);
					expect(std::ranges::count(expected.feedback, 0))
					    .to_be_less_than(static_cast<std::ptrdiff_t>(expected.feedback.size()));
					expect(rig.primary.isNative()).to_equal(config.tape == DelayTapeForm::NATIVE);
					expect(rig.result.wrapped).to_equal(expected.result.wrapped);
					expect(rig.result.secondary_written).to_equal(expected.result.secondary_written);
					expect(rig.primary_storage.size()).to_equal(kDelayStorageSize);
					if (config.growing) {
						const DelayTape* growing = rig.growing;
						expect(growing).to_equal(&rig.secondary);
						expect(interleaved({rig.secondary.begin(), rig.secondary.end()}))
						    .to_equal(interleaved(expected.secondary));
						expect(rig.secondary.head().pos - rig.secondary.begin())
						    .to_equal(expected.secondary_head.pos - expected.secondary.data());
						expect(rig.secondary.head().long_pos).to_equal(expected.secondary_head.long_pos);
						expect(rig.secondary.head().last_short_pos).to_equal(expected.secondary_head.last_short_pos);
						expect(rig.secondary.isNative()).to_be_true();
						// The secondary is written at its native rate, one step per block sample, and never wraps.
						expect(rig.result.secondary_written).to_equal(static_cast<int32_t>(n));
						expect(blocks * n).to_be_less_than(rig.secondary.size());
					}
					else {
						expect(rig.growing).to_be_null();
						expect(rig.secondary.isActive()).to_be_false();
						expect(rig.result.secondary_written).to_equal(int32_t{0});
					}
					// No run reaches the end of the primary, so the read head never wraps.
					expect(rig.result.wrapped).to_be_false();
				}
			}
		});

		it("renders each filter case as a direct call to a filter set per block, its noise draws counted, at every size", _ {
			const std::span<const Case> filters = cases(CaseList::FILTERS);
			// The calibration loop, each filter mono then stereo, the filter sets, then the two feedback chain controls.
			expect(filters.size()).to_equal((2 * kFilterCaseConfigs.size()) + kFilterSetRoutes.size() + 3);
			std::vector<std::tuple<FilterCaseConfig, bool, uint32_t>> runs;
			for (const auto& [config, draws] : kFilterCaseConfigs) {
				runs.emplace_back(config, false, draws);
				runs.emplace_back(config, true, draws);
			}
			for (const dsp::fixed::FilterRoute route : kFilterSetRoutes) {
				runs.emplace_back(filter_set_case(route), true, 1);
			}
			for (std::size_t i = 0; i < runs.size(); ++i) {
				const auto& [config, stereo, draws] = runs[i];
				const Case& c = filters[i + 1];
				expect(c.lanes).to_equal(stereo ? std::size_t{2} : std::size_t{1});
				for (const std::size_t n : kSizes) {
					const std::size_t blocks = RunConfig{}.blocks_per_run(n);
					const FilterReference expected = filter_reference(config, n, stereo, blocks);
					c.setup(component_of(c), n);
					for (std::size_t b = 0; b < blocks; ++b) {
						c.block(component_of(c), n);
					}
					const std::size_t samples = stereo ? 2 * n : n;
					expect(std::vector<q31>{lanes_output().begin(),
					                        lanes_output().begin() + static_cast<std::ptrdiff_t>(samples)})
					    .to_equal(expected.last_block);
					expect(std::ranges::count(expected.last_block, 0)).to_be_less_than(std::ptrdiff_t{2});
					expect(filter_rig().gain).to_equal(expected.gain);
					expect(jcong).to_equal(expected.noise);
					expect(jcong).to_equal(noise_after(kFilterNoiseSeed, blocks * samples * draws));
					expect(filter_rig().set.isLowPassOn()).to_equal(config.low.kind != dsp::fixed::LowPassSlot::Kind::OFF);
					expect(filter_rig().set.isHighPassOn())
					    .to_equal(config.high.kind != dsp::fixed::HighPassSlot::Kind::OFF);
				}
			}
		});

		it("runs tanh<3>, the low-pass and the high-pass as three passes and as one chain, alike, at every block size", _ {
			CaseSet& set = case_set();
			for (const std::size_t n : kSizes) {
				uint32_t low = 0;
				q31 high = 0;
				const std::vector<q31> expected = chain3_reference(n, low, high);
				expect(std::ranges::count(expected, 0)).to_be_less_than(std::ptrdiff_t{2});

				render(set.chain3_passes, n, 2);
				expect(std::vector<q31>{output().begin(), output().begin() + static_cast<std::ptrdiff_t>(n)})
				    .to_equal(expected);
				expect(set.chain3_passes.low_pass().state().z).to_equal(static_cast<q31>(low));
				expect(set.chain3_passes.high_pass().state().y).to_equal(high);

				render(set.chain3_fused, n, 2);
				expect(std::vector<q31>{output().begin(), output().begin() + static_cast<std::ptrdiff_t>(n)})
				    .to_equal(expected);
				const auto& fused = set.chain3_fused.component();
				expect(dsp::get<1>(fused.state()).z).to_equal(static_cast<q31>(low));
				expect(dsp::get<2>(fused.state()).y).to_equal(high);
				expect(dsp::get<1>(fused.coefficients()).m).to_equal(OnePoleLowPassCase::kMoveability);
			}
		});
	});
});

CPPSPEC_SPEC(bench_runner)
