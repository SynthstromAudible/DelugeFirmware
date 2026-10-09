#include "bench/cases.h"

#include "dsp-next/fixed/bandlimited/saw.h"
#include "dsp-next/fixed/bandlimited/square.h"
#include "dsp-next/fixed/noise/noise.h"
#include "dsp-next/fixed/sync/synced_oscillator.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace deluge::bench {
namespace {

using dsp::fixed::DuckEnvelope;

/// @brief The seed every input fill starts from.
constexpr uint32_t kSeed = 0x5EED'1234;

// The shared blocks, in SRAM .bss: every case reads and writes these, as the voice renders into its SRAM buffers.
alignas(16) std::array<q31, kMaxBlock> input_a_block{};
alignas(16) std::array<q31, kMaxBlock> input_b_block{};
alignas(16) std::array<q31, kMaxBlock> output_block{};
alignas(16) std::array<StereoSample, kMaxBlock> stereo_block{};

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
	fill(input_a_block, kSeed);
	fill(input_b_block, ~kSeed);
}

/// @return The first @p n samples of each shared q31 block.
[[nodiscard]] BlockSpans spans(std::size_t n) {
	return BlockSpans{
	    .a = dsp::fixed::Signal{input_a_block.data(), n},
	    .b = dsp::fixed::Signal{input_b_block.data(), n},
	    .out = dsp::fixed::Buffer{output_block.data(), n},
	};
}

} // namespace

std::span<const q31, kMaxBlock> input_a() {
	return input_a_block;
}

std::span<const q31, kMaxBlock> input_b() {
	return input_b_block;
}

std::span<const q31, kMaxBlock> output() {
	return output_block;
}

std::span<const StereoSample, kMaxBlock> stereo() {
	return stereo_block;
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
	out_ = dsp::Buffer<StereoSample>{stereo_block.data(), n};
	in_ = out_;
	dsp::fixed::SeededNoise noise{kSeed};
	for (StereoSample& frame : stereo_block) {
		frame.l = noise.render();
		frame.r = noise.render();
	}
	stereo_.configure({.gain = kGain});
}

void StereoGainCase::block(std::size_t /*n*/) {
	stereo_.renderBlock(in_, out_);
}

void DuckEnvelopeCase::setup(std::size_t /*n*/) {
	level_ = output_block.data();
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

CaseSet& case_set() {
	static CaseSet set;
	return set;
}

std::span<const Case> cases() {
	CaseSet& s = case_set();
	static const std::array list{
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
	};
	return list;
}

} // namespace deluge::bench
