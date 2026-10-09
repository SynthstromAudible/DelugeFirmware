#include "bench/cases.h"

#include "dsp-next/fixed/bandlimited/saw.h"
#include "dsp-next/fixed/bandlimited/square.h"
#include "dsp-next/fixed/noise/noise.h"
#include "dsp-next/fixed/sync/synced_oscillator.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace deluge::bench {
namespace {

using dsp::fixed::DuckEnvelope;
using dsp::fixed::OnePole;

/// @brief The seed every input fill starts from.
constexpr uint32_t kSeed = 0x5EED'1234;

/// @brief The q31 samples of padding after each shared q31 block: 256 bytes.
constexpr std::size_t kBlockGap = 64;

/// @brief The shared blocks, in SRAM .bss: every case reads and writes these, as the voice renders into its SRAM
/// buffers.
/// @details One object, so their distances are fixed by its layout rather than by where the linker places separate
/// globals. Each q31 block is followed by kBlockGap samples of padding, so no block sits a multiple of 1 KB from
/// another: the inputs are 768 bytes apart, the output 768 bytes after input_b and 1536 after input_a, and the stereo
/// block 768 bytes after the output, 1536 after input_b and 2304 after input_a. The lanes blocks follow, 128 bytes
/// past the stereo block's end and 256 bytes apart, which leaves every pair of blocks 256, 512 or 768 bytes off a
/// multiple of 1 KB. On the Cortex-A9 a load a multiple of 1 KB from a pending store waits on it (the check compares
/// partial addresses), which costs a mono case about 1 cycle per sample whenever its output sits a multiple of 1 KB
/// from its input.
struct SharedBlocks {
	alignas(16) std::array<q31, kMaxBlock> input_a;                  ///< The first input.
	std::array<q31, kBlockGap> after_a;                              ///< Padding, never read or written.
	alignas(16) std::array<q31, kMaxBlock> input_b;                  ///< The second input.
	std::array<q31, kBlockGap> after_b;                              ///< Padding, never read or written.
	alignas(16) std::array<q31, kMaxBlock> output;                   ///< The output.
	std::array<q31, kBlockGap> after_output;                         ///< Padding, never read or written.
	alignas(16) std::array<StereoSample, kMaxBlock> stereo;          ///< The stereo block, read and written in place.
	std::array<q31, kBlockGap / 2> after_stereo;                     ///< Padding, never read or written.
	alignas(16) std::array<q31, kMaxBlock * kMaxLanes> lanes_input;  ///< The lanes cases' input frames.
	std::array<q31, kBlockGap> after_lanes_input;                    ///< Padding, never read or written.
	alignas(16) std::array<q31, kMaxBlock * kMaxLanes> lanes_output; ///< The lanes cases' output frames.
};

/// @brief Whether the blocks at byte offsets @p a and @p b of SharedBlocks sit a multiple of 1 KB apart.
constexpr bool kilobyte_apart(std::size_t a, std::size_t b) {
	return ((a > b ? a - b : b - a) % 1024) == 0;
}

static_assert(!kilobyte_apart(offsetof(SharedBlocks, input_a), offsetof(SharedBlocks, input_b))
                  && !kilobyte_apart(offsetof(SharedBlocks, input_a), offsetof(SharedBlocks, output))
                  && !kilobyte_apart(offsetof(SharedBlocks, input_b), offsetof(SharedBlocks, output))
                  && !kilobyte_apart(offsetof(SharedBlocks, input_a), offsetof(SharedBlocks, stereo))
                  && !kilobyte_apart(offsetof(SharedBlocks, input_b), offsetof(SharedBlocks, stereo))
                  && !kilobyte_apart(offsetof(SharedBlocks, output), offsetof(SharedBlocks, stereo))
                  && !kilobyte_apart(offsetof(SharedBlocks, input_a), offsetof(SharedBlocks, lanes_input))
                  && !kilobyte_apart(offsetof(SharedBlocks, input_b), offsetof(SharedBlocks, lanes_input))
                  && !kilobyte_apart(offsetof(SharedBlocks, output), offsetof(SharedBlocks, lanes_input))
                  && !kilobyte_apart(offsetof(SharedBlocks, stereo), offsetof(SharedBlocks, lanes_input))
                  && !kilobyte_apart(offsetof(SharedBlocks, input_a), offsetof(SharedBlocks, lanes_output))
                  && !kilobyte_apart(offsetof(SharedBlocks, input_b), offsetof(SharedBlocks, lanes_output))
                  && !kilobyte_apart(offsetof(SharedBlocks, output), offsetof(SharedBlocks, lanes_output))
                  && !kilobyte_apart(offsetof(SharedBlocks, stereo), offsetof(SharedBlocks, lanes_output))
                  && !kilobyte_apart(offsetof(SharedBlocks, lanes_input), offsetof(SharedBlocks, lanes_output)),
              "no shared block sits a multiple of 1 KB from another");

SharedBlocks shared{};

/// @brief The wavetable's stored samples: each cycle, then its seven duplicates of the cycle's first samples.
constexpr std::size_t kWavetableStride = WavetableCase::kCycleSize + dsp::fixed::kWavetableDuplicateSamplesAtCycleEnd;
alignas(16) std::array<int16_t, WavetableCase::kCycles * kWavetableStride> wavetable_samples{};

/// @brief A band's increment limit as the loader sets it: one stored sample per output sample, times 1.25.
constexpr uint32_t kBandLimit = 0xFFFF'FFFFU >> WavetableCase::kCycleMagnitude;

/// @brief The wavetable's one band: every cycle, at the limit the loader gives a 128-sample band.
const dsp::fixed::WaveTableBand wavetable_band{
    .maxPhaseIncrement = kBandLimit + (kBandLimit / 4),
    .fromCycleNumber = 0,
    .toCycleNumber = WavetableCase::kCycles,
    .cycleSizeNoDuplicates = WavetableCase::kCycleSize,
    .cycleSizeMagnitude = WavetableCase::kCycleMagnitude,
    .dataAccessAddress = wavetable_samples.data(),
};

/// @brief Fills @p block from a fresh stream at @p seed.
void fill(std::span<q31> block, uint32_t seed) {
	dsp::fixed::SeededNoise noise{seed};
	for (q31& sample : block) {
		sample = noise.render();
	}
}

/// @brief Fills both q31 inputs from fresh streams, so every setup fills them identically.
void fill_inputs() {
	fill(shared.input_a, kSeed);
	fill(shared.input_b, ~kSeed);
}

/// @brief The lanes blocks' first @p n frames of type F: the q31 storage viewed as frames of interleaved lanes.
template <class F>
[[nodiscard]] FrameSpans<F> frame_spans(std::size_t n) {
	static_assert(sizeof(F) % sizeof(q31) == 0 && sizeof(F) <= kMaxLanes * sizeof(q31)
	              && std::is_trivially_copyable_v<F>);
	return FrameSpans<F>{
	    .in = dsp::Signal<F>{reinterpret_cast<const F*>(shared.lanes_input.data()), n},
	    .out = dsp::Buffer<F>{reinterpret_cast<F*>(shared.lanes_output.data()), n},
	};
}

/// @return The first @p n samples of each shared q31 block.
[[nodiscard]] BlockSpans spans(std::size_t n) {
	return BlockSpans{
	    .a = dsp::fixed::Signal{shared.input_a.data(), n},
	    .b = dsp::fixed::Signal{shared.input_b.data(), n},
	    .out = dsp::fixed::Buffer{shared.output.data(), n},
	};
}

} // namespace

std::span<const q31, kMaxBlock> input_a() {
	return shared.input_a;
}

std::span<const q31, kMaxBlock> input_b() {
	return shared.input_b;
}

std::span<const q31, kMaxBlock> output() {
	return shared.output;
}

std::span<const StereoSample, kMaxBlock> stereo() {
	return shared.stereo;
}

std::span<const q31, kMaxBlock * kMaxLanes> lanes_input() {
	return shared.lanes_input;
}

std::span<const q31, kMaxBlock * kMaxLanes> lanes_output() {
	return shared.lanes_output;
}

void CalibrationLoop::setup(std::size_t /*n*/) {
}

void CalibrationLoop::block(std::size_t n) {
	uint32_t count = static_cast<uint32_t>(n) * kIterationsPerSample;
	uint32_t product = 1;
	last_iterations_ = count;
	if (count != 0) {
#if defined(__arm__)
		asm volatile("1:\n\t"
		             "mul %1, %1, %2\n\t"
		             "mul %1, %1, %2\n\t"
		             "subs %0, %0, #1\n\t"
		             "bne 1b"
		             : "+r"(count), "+r"(product)
		             : "r"(kProductFactor)
		             : "cc");
#else
		while (count != 0) {
			product *= kProductFactor;
			asm volatile("" : "+r"(product));
			product *= kProductFactor;
			asm volatile("" : "+r"(product));
			--count;
			asm volatile("" : "+r"(count));
		}
#endif
	}
	last_product_ = product;
}

void AmplitudeCase::setup(std::size_t n) {
	spans_ = spans(n);
	fill_inputs();
	vca_ = dsp::fixed::Amplitude{kRamp};
}

void AmplitudeCase::block(std::size_t /*n*/) {
	vca_.renderBlock(spans_.a, spans_.b, spans_.out);
}

void CrudeAmplitudeCase::setup(std::size_t n) {
	spans_ = spans(n);
	fill_inputs();
	vca_ = dsp::fixed::CrudeAmplitude{kRamp};
}

void CrudeAmplitudeCase::block(std::size_t /*n*/) {
	vca_.renderBlock(spans_.a, spans_.b, spans_.out);
}

dsp::fixed::PulseOscillator PulseCase::make() {
	// The voice picks a pulse's band for 0.6 times the note's increment.
	const auto band_select = static_cast<uint32_t>(uint64_t{kA4Increment} * 3 / 5);
	return dsp::fixed::PulseOscillator{dsp::fixed::square_bandlimited_table().select(band_select), 0, kIncrement,
	                                   kPhaseOffset};
}

void PulseCase::setup(std::size_t n) {
	spans_ = spans(n);
	pulse_ = make();
}

void PulseCase::block(std::size_t /*n*/) {
	pulse_.renderBlock(spans_.out);
}

dsp::fixed::BandLimitedTableOscillator BandLimitedTableCase::make() {
	return dsp::fixed::BandLimitedTableOscillator{dsp::fixed::saw_bandlimited_table(), 0, kIncrement};
}

void BandLimitedTableCase::setup(std::size_t n) {
	spans_ = spans(n);
	oscillator_ = make();
}

void BandLimitedTableCase::block(std::size_t /*n*/) {
	oscillator_.renderBlock(spans_.out);
}

void CrudeSawCase::setup(std::size_t n) {
	spans_ = spans(n);
	saw_ = dsp::fixed::CrudeSaw{dsp::Phasor{0, kIncrement}};
}

void CrudeSawCase::block(std::size_t /*n*/) {
	saw_.renderBlock(spans_.out);
}

void CrudeSquareCase::setup(std::size_t n) {
	spans_ = spans(n);
	square_ = dsp::fixed::CrudeSquare{dsp::Phasor{0, kIncrement}, kPulseWidth};
}

void CrudeSquareCase::block(std::size_t /*n*/) {
	square_.renderBlock(spans_.out);
}

void CrudeTriangleCase::setup(std::size_t n) {
	spans_ = spans(n);
	triangle_ = dsp::fixed::CrudeTriangle{dsp::Phasor{0, kIncrement}};
}

void CrudeTriangleCase::block(std::size_t /*n*/) {
	triangle_.renderBlock(spans_.out);
}

void SyncedPulseCase::setup(std::size_t n) {
	spans_ = spans(n);
	master_ = dsp::Phasor{0, kMasterIncrement};
	audible_phase_ = 0;
}

void SyncedPulseCase::block(std::size_t n) {
	// As the voice's synced pulse: the pulse runs at half the audible phase and increment, its band picked for 0.6
	// times the audible increment, and the phase it ends on is stored back doubled.
	const uint32_t phase = audible_phase_ >> 1;
	constexpr uint32_t kIncrement = kAudibleIncrement >> 1;
	constexpr auto kBandSelect = static_cast<uint32_t>(uint64_t{kAudibleIncrement} * 3 / 5);
	const dsp::fixed::PulseOscillator pulse{dsp::fixed::square_bandlimited_table().select(kBandSelect), phase,
	                                        kIncrement, kPhaseOffset};
	dsp::fixed::SyncedOscillator synced{pulse, phase, kIncrement,
	                                    dsp::fixed::Resetter{master_.phase(), master_.increment()}, 0};
	synced.renderBlock(spans_.out);
	audible_phase_ = synced.phase() << 1;
	master_.seek(master_.phase() + (master_.increment() * static_cast<uint32_t>(n)));
}

dsp::fixed::WavetableOscillator WavetableCase::make() {
	// The loader rounds the transitions between cycles up to a power of two, and scales the wave index by them.
	constexpr int32_t kTransitions = kCycles - 1;
	constexpr int32_t kTransitionsMagnitude = 2;
	static_assert((1 << kTransitionsMagnitude) >= kTransitions && (1 << (kTransitionsMagnitude - 1)) < kTransitions);
	const dsp::fixed::WaveTableData table{kCycles,
	                                      kTransitionsMagnitude,
	                                      kTransitions << (31 - kTransitionsMagnitude),
	                                      &wavetable_band,
	                                      sizeof(dsp::fixed::WaveTableBand),
	                                      1};
	return dsp::fixed::WavetableOscillator{table, 0, kIncrement, kWaveIndex, 0};
}

void WavetableCase::setup(std::size_t n) {
	spans_ = spans(n);
	dsp::fixed::SeededNoise noise{kSeed};
	for (std::size_t cycle = 0; cycle < std::size_t{kCycles}; ++cycle) {
		const std::span<int16_t> stored{wavetable_samples.data() + (cycle * kWavetableStride), kWavetableStride};
		for (std::size_t i = 0; i < kCycleSize; ++i) {
			stored[i] = static_cast<int16_t>(noise.render() >> 16);
		}
		std::ranges::copy(stored.first(kWavetableStride - kCycleSize), stored.begin() + kCycleSize);
	}
	oscillator_ = make();
}

void WavetableCase::block(std::size_t /*n*/) {
	oscillator_.renderBlock(spans_.out);
}

void MonoGainCase::setup(std::size_t n) {
	spans_ = spans(n);
	fill_inputs();
	mono_.configure({.gain = kGain});
}

void MonoGainCase::block(std::size_t /*n*/) {
	mono_.renderBlock(spans_.a, spans_.out);
}

void MonoVecGainCase::setup(std::size_t n) {
	spans_ = spans(n);
	fill_inputs();
	mono_.configure({.gain = kGain});
}

void MonoVecGainCase::block(std::size_t /*n*/) {
	mono_.renderBlock(spans_.a, spans_.out);
}

void StereoGainCase::setup(std::size_t n) {
	out_ = dsp::Buffer<StereoSample>{shared.stereo.data(), n};
	in_ = out_;
	dsp::fixed::SeededNoise noise{kSeed};
	for (StereoSample& frame : shared.stereo) {
		frame.l = noise.render();
		frame.r = noise.render();
	}
	stereo_.configure({.gain = kGain});
}

void StereoGainCase::block(std::size_t /*n*/) {
	stereo_.renderBlock(in_, out_);
}

void DuckEnvelopeCase::setup(std::size_t /*n*/) {
	level_ = shared.output.data();
	duck_.configure(kSettings);
	duck_.reset();
	duck_.on<&DuckEnvelope::hit_at>(kHitStrength, kHitSamplesAgo);
}

void DuckEnvelopeCase::block(std::size_t n) {
	duck_.configure(kSettings);
	*level_ = duck_.advance(n);
}

void ShiftTrackerCase::setup(std::size_t n) {
	spans_ = spans(n);
	fill_inputs();
	mono_.configure({.mode = Tracker::Mode::REMOVE});
	mono_.reset();
}

void ShiftTrackerCase::block(std::size_t /*n*/) {
	mono_.renderBlock(spans_.a, spans_.out);
}

template <OnePole::Mode M>
void OnePoleCase<M>::setup(std::size_t n) {
	spans_ = spans(n);
	fill_inputs();
	mono_.configure({.moveability = kMoveability, .mode = M});
	mono_.reset();
}

template <OnePole::Mode M>
void OnePoleCase<M>::block(std::size_t /*n*/) {
	mono_.renderBlock(spans_.a, spans_.out);
}

template class OnePoleCase<OnePole::Mode::LOW_PASS>;
template class OnePoleCase<OnePole::Mode::ALL_PASS>;

void AntialiasedTanhCase::setup(std::size_t n) {
	spans_ = spans(n);
	fill_inputs();
	mono_.configure({.drive = kDrive});
	mono_.reset();
}

void AntialiasedTanhCase::block(std::size_t /*n*/) {
	mono_.renderBlock(spans_.a, spans_.out);
}

void Tanh3Case::setup(std::size_t n) {
	spans_ = spans(n);
	fill_inputs();
}

void Tanh3Case::block(std::size_t /*n*/) {
	mono_.renderBlock(spans_.a, spans_.out);
}

void DryWetBlendCase::setup(std::size_t n) {
	spans_ = spans(n);
	fill_inputs();
	mono_.configure({.level = kLevel});
}

void DryWetBlendCase::block(std::size_t /*n*/) {
	mono_.renderBlock(spans_.a, spans_.b, spans_.out);
}

template <std::size_t N>
void LanesOnePoleCase<N>::setup(std::size_t n) {
	spans_ = frame_spans<dsp::Frame<q31, N>>(n);
	fill(shared.lanes_input, kSeed);
	lanes_.configure({.moveability = kMoveability, .mode = OnePole::Mode::LOW_PASS});
	lanes_.reset();
}

template <std::size_t N>
void LanesOnePoleCase<N>::block(std::size_t /*n*/) {
	lanes_.renderBlock(spans_.in, spans_.out);
}

template class LanesOnePoleCase<2>;
template class LanesOnePoleCase<4>;

template <std::size_t N>
void LanesShiftTrackerCase<N>::setup(std::size_t n) {
	spans_ = frame_spans<dsp::Frame<q31, N>>(n);
	fill(shared.lanes_input, kSeed);
	lanes_.configure({.mode = ShiftTrackerCase::Tracker::Mode::REMOVE});
	lanes_.reset();
}

template <std::size_t N>
void LanesShiftTrackerCase<N>::block(std::size_t /*n*/) {
	lanes_.renderBlock(spans_.in, spans_.out);
}

template class LanesShiftTrackerCase<4>;
template class LanesShiftTrackerCase<8>;

void StereoAntialiasedTanhCase::setup(std::size_t n) {
	spans_ = frame_spans<StereoSample>(n);
	fill(shared.lanes_input, kSeed);
	stereo_.configure({.drive = kDrive});
	stereo_.reset();
}

void StereoAntialiasedTanhCase::block(std::size_t /*n*/) {
	stereo_.renderBlock(spans_.in, spans_.out);
}

CaseSet& case_set() {
	static CaseSet set;
	return set;
}

std::span<const Case> cases(CaseList list) {
	CaseSet& s = case_set();
	static const std::array kernels{
	    make_case("calibration_loop", s.calibration_loop),
	    make_case("amplitude", s.amplitude),
	    make_case("crude_amplitude", s.crude_amplitude),
	    make_case("pulse", s.pulse),
	    make_case("band_limited_table", s.band_limited_table),
	    make_case("crude_saw", s.crude_saw),
	    make_case("crude_square", s.crude_square),
	    make_case("crude_triangle", s.crude_triangle),
	    make_case("synced_pulse", s.synced_pulse),
	    make_case("wavetable", s.wavetable),
	    make_case("mono_gain", s.mono_gain),
	    make_case("mono_vec_gain", s.mono_vec_gain),
	    make_case("stereo_gain", s.stereo_gain),
	    make_case("duck_envelope", s.duck_envelope),
	    make_case("shift_tracker", s.shift_tracker),
	    make_case("one_pole_lp", s.one_pole_lp),
	    make_case("one_pole_ap", s.one_pole_ap),
	    make_case("antialiased_tanh", s.antialiased_tanh),
	    make_case("tanh3", s.tanh3),
	    make_case("dry_wet_blend", s.dry_wet_blend),
	};
	static const std::array lanes{
	    make_case("calibration_loop", s.calibration_loop),
	    make_case("one_pole_lp", s.one_pole_lp),
	    make_case("one_pole_x2", s.one_pole_x2),
	    make_case("one_pole_x4", s.one_pole_x4),
	    make_case("shift_tracker", s.shift_tracker),
	    make_case("shift_tracker_x4", s.shift_tracker_x4),
	    make_case("shift_tracker_x8", s.shift_tracker_x8),
	    make_case("antialiased_tanh", s.antialiased_tanh),
	    make_case("stereo_antialiased_tanh", s.stereo_antialiased_tanh),
	    make_case("mono_gain", s.mono_gain),
	    make_case("stereo_gain", s.stereo_gain),
	};
	if (list == CaseList::LANES) {
		return lanes;
	}
	return kernels;
}

} // namespace deluge::bench
