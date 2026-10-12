#pragma once

/// @file
/// @brief The DSP benchmark's case list: one case per dsp-next component, each configured as the voice configures it.
///
/// **Buffers.** Every case reads and writes the same statically allocated SRAM blocks: two q31 input blocks and one
/// q31 output block of kMaxBlock samples, one stereo block of kMaxBlock frames, and a lanes input and output block of
/// kMaxBlock frames of up to kMaxLanes lanes. Each case's `setup` fills the inputs it reads from a fixed-seed
/// SeededNoise, so every run times the same data, and a case measures the SRAM working set the voice renders in.
///
/// **Lanes.** A case of N lanes declares `kLanes = N` and renders blocks of n frames, N samples each; the runner
/// divides its cycles by N as well, so its cps is per channel-sample, comparable with the mono case of its kernel.
///
/// **Seven lists.** The device prints about 70 lines over RTT before it drops some, so the cases come in seven lists,
/// each run at every block size: the kernels (one case per component), the lanes (each `Lanes` case beside the mono
/// case of its kernel), the chain (stages run as separate passes beside the same stages as one `Chain`), the delay
/// (the whole delay block over real tapes), the filters (each filter and the filter set, as a voice runs them) and the
/// compressor (fully wet and blended, as its owners call it) and the mod FX (each type as its owners render it through
/// `effects::ModFx`). A bench image runs one, chosen by the CMake cache string DELUGE_DSP_BENCH_CASES, and builds only
/// that list's cases.
///
/// **State at timing.** The runner warms each case up before timing it, so what is timed is the case's steady
/// state. Each case's documentation says what that state is; the host spec checks each through its component's
/// public accessors.
///
/// **Rates.** Phase increments are UQ0.32 cycles per sample at 44.1 kHz: `frequency · 2^32 / 44100`.

#include "bench/reference_impulse_response.h"
#include "bench/runner.h"
#include "dsp-next/core/chain.h"
#include "dsp-next/core/control.h"
#include "dsp-next/core/frame.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/lanes.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/phasor.h"
#include "dsp-next/core/ramp.h"
#include "dsp-next/core/stereo.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/delay/delay_processor.h"
#include "dsp-next/fixed/delay/delay_tape.h"
#include "dsp-next/fixed/delay/feedback_shaper.h"
#include "dsp-next/fixed/dynamics/duck_envelope.h"
#include "dsp-next/fixed/dynamics/rms_feedback_compressor.h"
#include "dsp-next/fixed/filter/filter_modes.h"
#include "dsp-next/fixed/filter/filter_set.h"
#include "dsp-next/fixed/filter/one_pole.h"
#include "dsp-next/fixed/filter/shift_tracker.h"
#include "dsp-next/fixed/mix/dry_wet.h"
#include "dsp-next/fixed/osc/band_limited_table_oscillator.h"
#include "dsp-next/fixed/osc/crude_oscillators.h"
#include "dsp-next/fixed/osc/pulse_oscillator.h"
#include "dsp-next/fixed/shaping/antialiased_tanh.h"
#include "dsp-next/fixed/shaping/tanh_kernel.h"
#include "dsp-next/fixed/vca/amplitude.h"
#include "dsp-next/fixed/vca/crude_amplitude.h"
#include "dsp-next/fixed/wavetable/wave_table_data.h"
#include "dsp-next/fixed/wavetable/wavetable_oscillator.h"
#include "util/fixedpoint.h"

#include <algorithm>
#include <argon.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace deluge::bench {

using dsp::q31;
using dsp::StereoSample;

/// @brief The block sizes the device times every case at, in samples.
inline constexpr std::array<std::size_t, 3> kBlockSizes{32, 64, 128};

/// @brief The largest block a case renders, in samples (or frames): the buffers' size.
inline constexpr std::size_t kMaxBlock = 128;
static_assert(std::ranges::max(kBlockSizes) == kMaxBlock);

/// @brief The most samples a case renders from its setup to the end of its timed blocks, over kBlockSizes at the
/// runner's defaults: the warm-up and the K timed blocks, at the size where they come to most.
/// @details A case whose state drifts across a run stays representative for this many samples. At the defaults the
/// sizes run 80 blocks of 32 (2,560 samples), 72 of 64 (4,608) and 68 of 128 (8,704).
inline constexpr std::size_t kSamplesPerRun = [] {
	std::size_t most = 0;
	for (const std::size_t n : kBlockSizes) {
		most = std::max(most, RunConfig{}.blocks_per_run(n) * n);
	}
	return most;
}();
static_assert(kSamplesPerRun == 8'704);

/// @brief A4, 440 Hz: the mid-range pitch the band-limited and wavetable cases play at.
inline constexpr uint32_t kA4Increment = 42'852'282;
/// @brief A3, 220 Hz.
inline constexpr uint32_t kA3Increment = 21'426'141;
/// @brief A1, 55 Hz: inside the crude saw and square region (band index below 6, increments up to 7,040,929).
inline constexpr uint32_t kA1Increment = 5'356'535;

/// @brief The q31 input block every case's first input reads.
/// @return kMaxBlock samples, filled by the case's setup.
[[nodiscard]] std::span<const q31, kMaxBlock> input_a();

/// @brief The q31 input block a mixer's second input reads.
/// @return kMaxBlock samples, filled by the case's setup.
[[nodiscard]] std::span<const q31, kMaxBlock> input_b();

/// @brief The q31 block every mono case writes.
/// @return kMaxBlock samples.
[[nodiscard]] std::span<const q31, kMaxBlock> output();

/// @brief The stereo block the stereo case processes in place.
/// @return kMaxBlock frames, filled by the case's setup.
[[nodiscard]] std::span<const StereoSample, kMaxBlock> stereo();

/// @brief The most lanes a lanes case renders.
inline constexpr std::size_t kMaxLanes = 8;

/// @brief The block every lanes case reads: kMaxBlock frames of kMaxLanes lanes, as interleaved q31.
/// @return kMaxBlock · kMaxLanes samples, filled by the case's setup.
[[nodiscard]] std::span<const q31, kMaxBlock * kMaxLanes> lanes_input();

/// @brief The block every lanes case writes: kMaxBlock frames of kMaxLanes lanes, as interleaved q31.
/// @return kMaxBlock · kMaxLanes samples.
[[nodiscard]] std::span<const q31, kMaxBlock * kMaxLanes> lanes_output();

/// @brief The lanes blocks' first n frames of N lanes: the spans a lanes case's component renders between.
/// @details Built at setup, as BlockSpans are. The blocks are q31 storage viewed as frames, whose layout is N q31 in
/// lane order (frame.h), as `as_frames` views the app's interleaved buffers.
template <class F>
struct FrameSpans {
	dsp::Signal<F> in;  ///< lanes_input's first n frames.
	dsp::Buffer<F> out; ///< lanes_output's first n frames.
};

/// @brief The shared q31 blocks' first n samples: the spans a mono case's component renders between.
/// @details A case builds these at setup, for the block size it is set up for, so that its timed `block` passes them
/// by copy and makes no call besides its component's own: under the app's `-fno-inline-functions`, building a span
/// is an out-of-line call, which the calibration would not cancel.
struct BlockSpans {
	dsp::fixed::Signal a;   ///< input_a's first n samples.
	dsp::fixed::Signal b;   ///< input_b's first n samples.
	dsp::fixed::Buffer out; ///< The output block's first n samples.
};

/// @brief A q31 gain: the reference processor kernel, as in the Mono and Stereo unit specs.
struct Gain {
	using value_type = q31; ///< Q31 samples.
	using step_type = q31;  ///< One sample per step.
	/// @brief The gain to apply.
	struct Settings {
		q31 gain; ///< Q31 in [-1, 1); 0x7FFFFFFF is unity.
	};
	/// @brief The gain, as configured.
	struct Coefficients {
		q31 gain; ///< Q31 in [-1, 1).
	};
	/// @brief A gain is stateless.
	struct State {};
	/// @return The gain, unchanged.
	static Coefficients coefficients(const Settings& s) { return {.gain = s.gain}; }
	/// @return @p x · gain, Q31: the high word of the Q62 product is Q30, shifted up by one.
	[[gnu::always_inline]] static q31 step(const Coefficients& c, const dsp::EmptyBlock& /*block*/, State& /*state*/,
	                                       q31 x) {
		return multiply_32x32_rshift32(x, c.gain) << 1;
	}
};
static_assert(dsp::processor_kernel<Gain>);

/// @brief Gain on four lanes at a time: the reference Argon processor kernel.
/// @details One saturating doubling high multiply (`vqdmulh`) per vector, which equals Gain's arithmetic except that
/// it saturates the one product, -1 · -1, that Gain wraps.
struct VecGain {
	using value_type = q31;       ///< Q31 samples.
	using step_type = Argon<q31>; ///< One vector of four samples per step.
	using Settings = Gain::Settings;
	using Coefficients = Gain::Coefficients;
	/// @brief A gain is stateless.
	struct State {};
	/// @return The gain, unchanged.
	static Coefficients coefficients(const Settings& s) { return {.gain = s.gain}; }
	/// @return Each lane of @p x · gain, Q31.
	[[gnu::always_inline]] static Argon<q31> step(const Coefficients& c, const dsp::EmptyBlock& /*block*/,
	                                              State& /*state*/, Argon<q31> x) {
		return x.MultiplyFixedQMax(c.gain);
	}
};
static_assert(dsp::processor_kernel<VecGain>);

/// @brief A loop of known instruction and cycle counts: the sanity case for the instruction and cycle counters.
/// @details Each block runs kIterationsPerSample · n iterations of a four-instruction loop, written in assembly on ARM
/// so that the compiler cannot change it: two `mul`s, each multiplying the product by kProductFactor and so consuming
/// the other's result, then `subs` and `bne`.
///
/// **Instructions.** One timed run of K = 64 blocks renames `K · 4 · kIterationsPerSample · n = 1024 · n` loop
/// instructions: 32,768 at n = 32, 65,536 at n = 64 and 131,072 at n = 128. The calibration runs the same trampoline
/// into an empty `block`, so it cancels the calls; beyond the loop, each block adds a small constant it does not
/// cancel: the count's and the product's setup and stores, the frame `block` keeps, and the instructions renamed down
/// the predicted path when the loop's last `bne` mispredicts. That constant is under 32 instructions, so the reported
/// `inst` lies in [1024 · n, 1024 · n + 2048].
///
/// **Cycles.** The A9 TRM (r4p1, Table B.5) gives `MUL` a result latency of 4 cycles. Each `mul` waits for the one
/// before it, so an iteration takes at least 8 cycles: a run takes at least `K · 8 · kIterationsPerSample · n =
/// 2048 · n` cycles (65,536, 131,072 and 262,144), and `cps` is at least 32.00. The loop thus retires at most one
/// instruction per two cycles, so an event counting cycles in place of instructions reads at least twice the `inst`
/// band's bottom (2 · 1024 · n), which is above the band's top, and the band check fails for it.
///
/// Off ARM the loop is plain C++, with empty asms keeping the count and the product live.
///
/// **Timed state:** stateless; every block runs the same loop from a product of 1.
class CalibrationLoop {
public:
	/// @brief Loop iterations per sample of the block.
	static constexpr uint32_t kIterationsPerSample = 4;
	/// @brief What each `mul` multiplies the product by: odd, and 3 mod 8, so its powers repeat only every 2^30.
	static constexpr uint32_t kProductFactor = 3;

	/// @brief Nothing to configure or fill.
	void setup(std::size_t n);
	/// @brief Runs kIterationsPerSample · @p n iterations.
	void block(std::size_t n);
	/// @return The iterations the last block ran.
	[[nodiscard]] uint32_t last_iterations() const { return last_iterations_; }
	/// @return The product the last block's multiplies ended on: kProductFactor^(2 · iterations), modulo 2^32.
	[[nodiscard]] uint32_t last_product() const { return last_product_; }

private:
	uint32_t last_iterations_ = 0;
	uint32_t last_product_ = 1;
};

/// @brief The band-limited VCA, `Amplitude`, mixing input_a (the raw wave) into input_b (the mix) into the output.
/// @details **Timed state:** a mid-level ramp, a quarter of full scale (2^29) rising 1,024 per sample. A fresh
/// Amplitude is built from it at setup and runs every block from there, as one long block. Over a whole run (at most
/// kSamplesPerRun samples) the ramp rises by under 9 · 10^6, so it stays mid-level and never saturates: every block is
/// the same arithmetic on the same inputs.
class AmplitudeCase {
public:
	/// @brief The ramp the VCA is built from: a start of 2^29, a quarter of full scale (Q31), rising 1,024 per sample.
	static constexpr dsp::Ramp kRamp{int32_t{1} << 29, 1024};

	/// @brief Builds the VCA from kRamp, fills both inputs and takes the blocks' spans for @p n.
	void setup(std::size_t n);
	/// @brief Mixes one block of the size it was set up for.
	void block(std::size_t n);
	/// @return The ramp the VCA was built from.
	[[nodiscard]] static constexpr dsp::Ramp ramp() { return kRamp; }

private:
	dsp::fixed::Amplitude vca_{kRamp};
	BlockSpans spans_{};
};
static_assert(AmplitudeCase::kRamp.value() > 0 && AmplitudeCase::kRamp.increment() > 0);
static_assert(int64_t{AmplitudeCase::kRamp.value()}
                  + (int64_t{AmplitudeCase::kRamp.increment()} * int64_t{kSamplesPerRun})
              < int64_t{std::numeric_limits<int32_t>::max() / 2});

/// @brief The crude VCA, `CrudeAmplitude`, mixing input_a into input_b into the output.
/// @details **Timed state:** the same mid-level ramp as AmplitudeCase, built at setup and run every block from there;
/// it rises by under 9 · 10^6 over a whole run, so it neither overflows nor saturates.
class CrudeAmplitudeCase {
public:
	/// @brief The ramp the VCA is built from.
	static constexpr dsp::Ramp kRamp = AmplitudeCase::kRamp;

	/// @brief Builds the VCA from kRamp, fills both inputs and takes the blocks' spans for @p n.
	void setup(std::size_t n);
	/// @brief Mixes one block of the size it was set up for.
	void block(std::size_t n);
	/// @return The ramp the VCA was built from.
	[[nodiscard]] static constexpr dsp::Ramp ramp() { return kRamp; }

private:
	dsp::fixed::CrudeAmplitude vca_{kRamp};
	BlockSpans spans_{};
};
static_assert(CrudeAmplitudeCase::kRamp.value() > 0 && CrudeAmplitudeCase::kRamp.increment() > 0);
static_assert(int64_t{CrudeAmplitudeCase::kRamp.value()}
                  + (int64_t{CrudeAmplitudeCase::kRamp.increment()} * int64_t{kSamplesPerRun})
              < int64_t{std::numeric_limits<int32_t>::max() / 2});

/// @brief The perfect pulse, `PulseOscillator`, rendering into the output.
/// @details The voice renders a square through PulseOscillator only at a non-zero pulse width; at 50% it plays the
/// band-limited square table. The case uses the 50% width's offset, -(2^31 / 2) = 0xC0000000 between the two squares,
/// to stand in for any non-zero width: the pulse's arithmetic is the same whatever the offset. The rest is as the
/// voice configures a pulse: half the note's increment (the pulse runs at half rate), and the square band picked for
/// 0.6 times the note's increment.
///
/// **Timed state:** free-running at A4 from phase 0. The pulse renders whole vectors, and every block size is a
/// multiple of four, so each block advances the phase by exactly n · increment and the phase wraps freely.
class PulseCase {
public:
	/// @brief The pulse's increment: half of A4's.
	static constexpr uint32_t kIncrement = kA4Increment >> 1;
	/// @brief The phase between the two squares: the 50% width's, standing in for any width.
	static constexpr uint32_t kPhaseOffset = 0xC000'0000;

	/// @brief Restarts the pulse at phase 0, and takes the output's span for @p n.
	void setup(std::size_t n);
	/// @brief Renders one block of the size it was set up for.
	void block(std::size_t n);
	/// @return The pulse's phase.
	[[nodiscard]] uint32_t phase() const { return pulse_.phase(); }

private:
	dsp::fixed::PulseOscillator pulse_{make()};
	BlockSpans spans_{};
	[[nodiscard]] static dsp::fixed::PulseOscillator make();
};

/// @brief The band-limited saw, `BandLimitedTableOscillator`, rendering into the output.
/// @details **Timed state:** free-running at A4 from phase 0, on the saw band A4 selects (band 11, 2^11 samples). Each
/// block advances the phase by n · increment.
class BandLimitedTableCase {
public:
	/// @brief The oscillator's increment: A4.
	static constexpr uint32_t kIncrement = kA4Increment;

	/// @brief Restarts the oscillator at phase 0, and takes the output's span for @p n.
	void setup(std::size_t n);
	/// @brief Renders one block of the size it was set up for.
	void block(std::size_t n);
	/// @return The oscillator's phase.
	[[nodiscard]] uint32_t phase() const { return oscillator_.phase(); }

private:
	dsp::fixed::BandLimitedTableOscillator oscillator_{make()};
	BlockSpans spans_{};
	[[nodiscard]] static dsp::fixed::BandLimitedTableOscillator make();
};

/// @brief The crude saw, `CrudeSaw<Phasor>`, rendering into the output.
/// @details **Timed state:** free-running at A1, inside the voice's crude saw region, from phase 0. Each block advances
/// the phase by n · increment.
class CrudeSawCase {
public:
	/// @brief The saw's increment: A1.
	static constexpr uint32_t kIncrement = kA1Increment;

	/// @brief Restarts the saw at phase 0, and takes the output's span for @p n.
	void setup(std::size_t n);
	/// @brief Renders one block of the size it was set up for.
	void block(std::size_t n);
	/// @return The saw's phase.
	[[nodiscard]] uint32_t phase() const { return saw_.phase(); }

private:
	dsp::fixed::CrudeSaw<dsp::Phasor> saw_{dsp::Phasor{0, kIncrement}};
	BlockSpans spans_{};
};

/// @brief The crude square, `CrudeSquare<Phasor>`, rendering into the output.
/// @details At a 50% width: the voice's threshold for a width of 0 is half a cycle, 2^31.
///
/// **Timed state:** free-running at A1, inside the voice's crude square region, from phase 0. Each block advances the
/// phase by n · increment.
class CrudeSquareCase {
public:
	/// @brief The square's increment: A1.
	static constexpr uint32_t kIncrement = kA1Increment;
	/// @brief The square's threshold phase: half a cycle.
	static constexpr uint32_t kPulseWidth = 0x8000'0000;

	/// @brief Restarts the square at phase 0, and takes the output's span for @p n.
	void setup(std::size_t n);
	/// @brief Renders one block of the size it was set up for.
	void block(std::size_t n);
	/// @return The square's phase.
	[[nodiscard]] uint32_t phase() const { return square_.phase(); }

private:
	dsp::fixed::CrudeSquare<dsp::Phasor> square_{dsp::Phasor{0, kIncrement}, kPulseWidth};
	BlockSpans spans_{};
};

/// @brief The crude triangle, `CrudeTriangle<Phasor>`, rendering into the output.
/// @details **Timed state:** free-running at A4, from phase 0: the voice renders the triangle crude below 69,273,666
/// (about 711 Hz), so A4 is inside its crude region. Each block advances the phase by n · increment.
class CrudeTriangleCase {
public:
	/// @brief The triangle's increment: A4.
	static constexpr uint32_t kIncrement = kA4Increment;

	/// @brief Restarts the triangle at phase 0, and takes the output's span for @p n.
	void setup(std::size_t n);
	/// @brief Renders one block of the size it was set up for.
	void block(std::size_t n);
	/// @return The triangle's phase.
	[[nodiscard]] uint32_t phase() const { return triangle_.phase(); }

private:
	dsp::fixed::CrudeTriangle<dsp::Phasor> triangle_{dsp::Phasor{0, kIncrement}};
	BlockSpans spans_{};
};

/// @brief The hard-synced perfect pulse, `SyncedOscillator<PulseOscillator>`, rendering into the output.
/// @details Built every block, as the voice's synced pulse arm builds it: the pulse at half the audible increment and
/// half its phase, synced to the master's phase and increment, and the audible phase stored back doubled. The master
/// is a Phasor the case advances by the block after rendering it, as the master oscillator's own render would. A
/// SyncedOscillator never advances its resetter, so one kept across blocks would restart every block from the same
/// master phase and replay the same windows; building it every block makes the crossovers fall where the master's
/// phase puts them.
///
/// **Timed state:** the audible pulse at 2.5 times the master's pitch (A3), so a crossover every
/// 2^32 / kMasterIncrement ≈ 200 samples. Over the K timed blocks that is about K · n / 200 crossovers: 10 at n = 32,
/// 20 at n = 64 and 41 at n = 128.
class SyncedPulseCase {
public:
	/// @brief The master's increment: A3.
	static constexpr uint32_t kMasterIncrement = kA3Increment;
	/// @brief The audible increment: 2.5 times the master's.
	static constexpr uint32_t kAudibleIncrement = kMasterIncrement * 5 / 2;
	/// @brief The phase between the pulse's two squares for a 50% width.
	static constexpr uint32_t kPhaseOffset = PulseCase::kPhaseOffset;

	/// @brief Restarts the master and the audible phase at 0, and takes the output's span for @p n.
	void setup(std::size_t n);
	/// @brief Renders one block of the size it was set up for, then advances the master by it.
	void block(std::size_t n);
	/// @return The master's phase.
	[[nodiscard]] uint32_t master_phase() const { return master_.phase(); }
	/// @return The audible oscillator's phase, at the full (not halved) rate.
	[[nodiscard]] uint32_t audible_phase() const { return audible_phase_; }

private:
	dsp::Phasor master_{0, kMasterIncrement};
	uint32_t audible_phase_ = 0;
	BlockSpans spans_{};
};

/// @brief The wavetable oscillator, `WavetableOscillator`, over a small morphing wavetable built in SRAM, rendering
/// into the output.
/// @details The wavetable has four cycles of 128 samples (plus the seven duplicate samples each stored cycle carries)
/// in one band, filled from the noise stream. Its band covers increments up to (2^32 - 1 >> 7) · 1.25 = 41,943,038,
/// as the loader sets a 128-sample band's limit. The wave index is the voice's middle position, 2^30, which scales to
/// halfway between cycles 1 and 2.
///
/// **Timed state:** free-running at A3 from phase 0, morphing between cycles 1 and 2 at a fixed blend (no wave-index
/// sweep), so every block is one chunk and advances the phase by n · increment.
class WavetableCase {
public:
	/// @brief The oscillator's increment: A3.
	static constexpr uint32_t kIncrement = kA3Increment;
	/// @brief The cycles in the wavetable.
	static constexpr int32_t kCycles = 4;
	/// @brief log2 of a cycle's samples.
	static constexpr uint8_t kCycleMagnitude = 7;
	/// @brief A cycle's samples, without the duplicates.
	static constexpr uint16_t kCycleSize = uint16_t{1} << kCycleMagnitude;
	/// @brief The wave index: the voice's middle position.
	static constexpr int32_t kWaveIndex = int32_t{1} << 30;

	/// @brief Fills the wavetable and restarts the oscillator at phase 0, and takes the output's span for @p n.
	void setup(std::size_t n);
	/// @brief Renders one block of the size it was set up for.
	void block(std::size_t n);
	/// @return The oscillator's phase.
	[[nodiscard]] uint32_t phase() const { return oscillator_.phase(); }

private:
	dsp::fixed::WavetableOscillator oscillator_{make()};
	BlockSpans spans_{};
	[[nodiscard]] static dsp::fixed::WavetableOscillator make();
};

/// @brief `Mono<Gain>`: a scalar q31 kernel, from input_a into the output.
/// @details **Timed state:** stateless, at -3 dB (0x5A82799A); every block is the same arithmetic on the same input.
class MonoGainCase {
public:
	/// @brief The gain: -3 dB, Q31.
	static constexpr q31 kGain = 0x5A82'799A;

	/// @brief Configures the gain, fills input_a and takes the blocks' spans for @p n.
	void setup(std::size_t n);
	/// @brief Processes one block of the size it was set up for.
	void block(std::size_t n);
	/// @return The component.
	[[nodiscard]] const dsp::Mono<Gain>& component() const { return mono_; }

private:
	dsp::Mono<Gain> mono_;
	BlockSpans spans_{};
};

/// @brief `Mono<VecGain>`: an Argon q31 kernel, from input_a into the output.
/// @details **Timed state:** stateless, at -3 dB; every block is the same arithmetic on the same input.
class MonoVecGainCase {
public:
	/// @brief The gain: -3 dB, Q31.
	static constexpr q31 kGain = MonoGainCase::kGain;

	/// @brief Configures the gain, fills input_a and takes the blocks' spans for @p n.
	void setup(std::size_t n);
	/// @brief Processes one block of the size it was set up for.
	void block(std::size_t n);
	/// @return The component.
	[[nodiscard]] const dsp::Mono<VecGain>& component() const { return mono_; }

private:
	dsp::Mono<VecGain> mono_;
	BlockSpans spans_{};
};

/// @brief `Stereo<Gain>`: a scalar q31 kernel on both channels, processing the stereo block in place, as the app's
/// stereo buffers are processed.
/// @details **Timed state:** stateless, at unity (0x7FFFFFFF). In place, each pass lowers a sample by at most two
/// LSBs, so the frames keep their level for the whole run and every block is the same arithmetic on full-scale data.
class StereoGainCase {
public:
	static constexpr std::size_t kLanes = 2; ///< Left and right.
	/// @brief The gain: unity, Q31.
	static constexpr q31 kGain = 0x7FFF'FFFF;

	/// @brief Configures the gain, fills the stereo block and takes its span for @p n.
	void setup(std::size_t n);
	/// @brief Processes one block of the size it was set up for, in place.
	void block(std::size_t n);
	/// @return The component.
	[[nodiscard]] const dsp::Stereo<Gain>& component() const { return stereo_; }

private:
	dsp::Stereo<Gain> stereo_;
	dsp::Signal<StereoSample> in_{};  ///< The stereo block's first n frames, read.
	dsp::Buffer<StereoSample> out_{}; ///< The same frames, written.
};

/// @brief `Mono<ShiftTracker<11>>` in REMOVE: the delay's DC blocker, from input_a into the output.
/// @details **Timed state:** the memory carries from block to block, from silence at setup, tracking the slow part of
/// the one input block the case repeats. Every block is the same arithmetic: one subtract, shift and add to update the
/// memory, and one subtract for the output.
class ShiftTrackerCase {
public:
	/// @brief The kernel: the delay's blocker, at a shift of 11.
	using Tracker = dsp::fixed::ShiftTracker<11>;

	/// @brief Configures the tracker in REMOVE from silence, fills input_a and takes the blocks' spans for @p n.
	void setup(std::size_t n);
	/// @brief Processes one block of the size it was set up for.
	void block(std::size_t n);
	/// @return The component.
	[[nodiscard]] const dsp::Mono<Tracker>& component() const { return mono_; }

private:
	dsp::Mono<Tracker> mono_;
	BlockSpans spans_{};
};

/// @brief `Mono<OnePole>` in mode @p M at the granular kernel's moveability, from input_a into the output.
/// @details **Timed state:** the memory carries from block to block, from silence at setup, following the one input
/// block the case repeats. Every block is the same arithmetic: a subtract, a rounding multiply and two shifted adds to
/// update the memory, and in ALL_PASS one reverse subtract for the output.
/// @tparam M The mode: LOW_PASS for `one_pole_lp`, ALL_PASS for `one_pole_ap`.
template <dsp::fixed::OnePole::Mode M>
class OnePoleCase {
public:
	/// @brief The moveability, Q31: the granular kernel's wet low-pass. m = 1/4 gives tan(πf/fs) = 1/3, a cutoff of
	/// about 4.5 kHz at 44.1 kHz.
	static constexpr q31 kMoveability = 1 << 29;

	/// @brief Configures the pole in @p M from silence, fills input_a and takes the blocks' spans for @p n.
	void setup(std::size_t n);
	/// @brief Processes one block of the size it was set up for.
	void block(std::size_t n);
	/// @return The component.
	[[nodiscard]] const dsp::Mono<dsp::fixed::OnePole>& component() const { return mono_; }

private:
	dsp::Mono<dsp::fixed::OnePole> mono_;
	BlockSpans spans_{};
};

/// @brief `one_pole_lp`: the low-pass.
using OnePoleLowPassCase = OnePoleCase<dsp::fixed::OnePole::Mode::LOW_PASS>;
/// @brief `one_pole_ap`: the all-pass.
using OnePoleAllPassCase = OnePoleCase<dsp::fixed::OnePole::Mode::ALL_PASS>;

/// @brief `Mono<AntialiasedTanh>` at drive 3, the compressor's, from input_a into the output.
/// @details **Timed state:** the position carries from block to block, from the centre at setup, following the one
/// input block the case repeats. Every block is the same arithmetic: a saturating shift for the position, two linear
/// reads of the 2-D table and their blend, and the output shift.
class AntialiasedTanhCase {
public:
	/// @brief The drive as a power of two: the compressor's.
	static constexpr uint32_t kDrive = 3;

	/// @brief Configures the drive from the centre, fills input_a and takes the blocks' spans for @p n.
	void setup(std::size_t n);
	/// @brief Processes one block of the size it was set up for.
	void block(std::size_t n);
	/// @return The component.
	[[nodiscard]] const dsp::Mono<dsp::fixed::AntialiasedTanh>& component() const { return mono_; }

private:
	dsp::Mono<dsp::fixed::AntialiasedTanh> mono_;
	BlockSpans spans_{};
};

/// @brief `Mono<Tanh<3>>`: tanh at drive 3, stateless, from input_a into the output.
/// @details **Timed state:** none. Every block is the same arithmetic: a saturating shift for the position, one linear
/// read of the small table, and the output shift.
class Tanh3Case {
public:
	/// @brief The kernel: tanh at drive 3.
	using Kernel = dsp::fixed::Tanh<3>;

	/// @brief Fills input_a and takes the blocks' spans for @p n.
	void setup(std::size_t n);
	/// @brief Processes one block of the size it was set up for.
	void block(std::size_t n);
	/// @return The component.
	[[nodiscard]] const dsp::Mono<Kernel>& component() const { return mono_; }

private:
	dsp::Mono<Kernel> mono_;
	BlockSpans spans_{};
};

/// @brief `Mono<DryWetBlend>` as a mixer at a mid level, blending input_a (dry) with input_b (wet) into the output.
/// @details **Timed state:** none. Every block is the same arithmetic in BLEND: a truncating multiply for the wet
/// product, a rounding multiply-accumulate of the dry one, and the doubling.
class DryWetBlendCase {
public:
	/// @brief The wet level, Q31: a half.
	static constexpr q31 kLevel = 1 << 30;

	/// @brief Configures the level, fills the inputs and takes the blocks' spans for @p n.
	void setup(std::size_t n);
	/// @brief Mixes one block of the size it was set up for.
	void block(std::size_t n);
	/// @return The component.
	[[nodiscard]] const dsp::Mono<dsp::fixed::DryWetBlend>& component() const { return mono_; }

private:
	dsp::Mono<dsp::fixed::DryWetBlend> mono_;
	BlockSpans spans_{};
};

/// @brief `Lanes<OnePole, N>` in LOW_PASS at the granular kernel's moveability, op-major: N streams through one
/// pole, from the lanes input into the lanes output.
/// @details **Timed state:** every lane's memory carries from block to block, from silence at setup, following its
/// own stream of the one input block the case repeats. Every frame is the same arithmetic: per lane a subtract, a
/// rounding multiply and two shifted adds.
/// @tparam N The lane count: 2 for `one_pole_x2`, 4 for `one_pole_x4`.
template <std::size_t N>
class LanesOnePoleCase {
public:
	static constexpr std::size_t kLanes = N; ///< The lanes per frame.
	/// @brief The moveability, Q31: the mono case's.
	static constexpr q31 kMoveability = OnePoleLowPassCase::kMoveability;
	using Component = dsp::Lanes<dsp::fixed::OnePole, N>; ///< The component timed.

	/// @brief Configures the pole from silence, fills the lanes input and takes the blocks' spans for @p n frames.
	void setup(std::size_t n);
	/// @brief Processes one block of the size it was set up for.
	void block(std::size_t n);
	/// @return The component.
	[[nodiscard]] const Component& component() const { return lanes_; }

private:
	Component lanes_;
	FrameSpans<dsp::Frame<q31, N>> spans_{};
};

/// @brief `one_pole_x2`.
using OnePoleX2Case = LanesOnePoleCase<2>;
/// @brief `one_pole_x4`.
using OnePoleX4Case = LanesOnePoleCase<4>;

/// @brief `Lanes<ShiftTracker<11>, N>` in REMOVE, op-major: N streams through the delay's DC blocker, from the lanes
/// input into the lanes output.
/// @details **Timed state:** every lane's memory carries from block to block, from silence at setup, tracking its own
/// stream of the one input block the case repeats. Every frame is the same arithmetic: per lane a subtract, a shifted
/// add and a subtract.
/// @tparam N The lane count: 4 for `shift_tracker_x4`, 8 for `shift_tracker_x8`.
template <std::size_t N>
class LanesShiftTrackerCase {
public:
	static constexpr std::size_t kLanes = N;                    ///< The lanes per frame.
	using Component = dsp::Lanes<ShiftTrackerCase::Tracker, N>; ///< The component timed.

	/// @brief Configures the tracker in REMOVE from silence, fills the lanes input and takes the blocks' spans for @p n
	/// frames.
	void setup(std::size_t n);
	/// @brief Processes one block of the size it was set up for.
	void block(std::size_t n);
	/// @return The component.
	[[nodiscard]] const Component& component() const { return lanes_; }

private:
	Component lanes_;
	FrameSpans<dsp::Frame<q31, N>> spans_{};
};

/// @brief `shift_tracker_x4`.
using ShiftTrackerX4Case = LanesShiftTrackerCase<4>;
/// @brief `shift_tracker_x8`.
using ShiftTrackerX8Case = LanesShiftTrackerCase<8>;

/// @brief `Stereo<AntialiasedTanh>` at drive 3, lane by lane: the lanes input's first 2·n words, read as n
/// consecutive left/right frames, into the lanes output's first 2·n words.
/// @details **Timed state:** each channel's position carries from block to block, from the centre at setup, following
/// its channel of the one input block the case repeats. Every frame is the mono case's arithmetic twice, left then
/// right.
class StereoAntialiasedTanhCase {
public:
	static constexpr std::size_t kLanes = 2; ///< Left and right.
	/// @brief The drive as a power of two: the mono case's.
	static constexpr uint32_t kDrive = AntialiasedTanhCase::kDrive;

	/// @brief Configures the drive from the centre, fills the lanes input and takes the blocks' spans for @p n frames.
	void setup(std::size_t n);
	/// @brief Processes one block of the size it was set up for.
	void block(std::size_t n);
	/// @return The component.
	[[nodiscard]] const dsp::Stereo<dsp::fixed::AntialiasedTanh>& component() const { return stereo_; }

private:
	dsp::Stereo<dsp::fixed::AntialiasedTanh> stereo_;
	FrameSpans<StereoSample> spans_{};
};

/// @brief `Control<DuckEnvelope>`: the sidechain's duck, configured then advanced once per block, as its owner does.
/// @details The rates are as the owner resolves them: the spec's attack of 37,640 per sample (222 samples), and a
/// release of 472 per sample (the release table's entry 40, 59 << 3: 17,772 samples), at the middle shape (fully
/// curved at the release's start). Setup places a hit of 0x60000000 4,665 samples ago: 222 samples of attack, then
/// 4,443 into the release, at position 4,443 · 472 = 2,097,096, a quarter of the stage.
///
/// **Timed state:** mid-release. Over a whole run the position rises by at most kSamplesPerRun · 472 = 8,704 · 472 =
/// 4,108,288, to 6,205,384, short of the stage's end at 2^23 = 8,388,608, and its curvedness, 65,536 - position / 128,
/// stays above zero: every block is the same curved-release arithmetic. Its cycles per sample, like every case's,
/// divide the block's cost by n, though the duck does its work once per block.
class DuckEnvelopeCase {
public:
	/// @brief The rates and shape, as the owner resolves them.
	static constexpr dsp::fixed::DuckEnvelope::Settings kSettings{
	    .attack = 37'640, .release = 472, .shape = 0, .instant_attack = false};
	/// @brief The hit's strength, Q31.
	static constexpr q31 kHitStrength = 0x6000'0000;
	/// @brief How long ago the hit landed, in samples.
	static constexpr uint32_t kHitSamplesAgo = 4'665;

	/// @brief Configures the duck, places the hit and takes the output block's first sample for the level.
	void setup(std::size_t n);
	/// @brief Configures the duck and advances it by one block, writing its level to the output block's first sample.
	void block(std::size_t n);
	/// @return The component.
	[[nodiscard]] const dsp::Control<dsp::fixed::DuckEnvelope>& component() const { return duck_; }

private:
	dsp::Control<dsp::fixed::DuckEnvelope> duck_;
	q31* level_ = nullptr; ///< The output block's first sample, which receives the level.
};

/// @brief Fills the lanes input with stereo noise at under half of full scale: each sample of the noise halved.
void fill_delay_input();

/// @brief The lanes blocks' first @p n frames, viewed as stereo frames.
/// @return The spans a stereo case renders between, built at setup.
[[nodiscard]] FrameSpans<StereoSample> stereo_frame_spans(std::size_t n);

/// @brief The delay's high-pass, as its feedback chain pins it: the shift tracker taking its memory out.
using DelayHighPass = dsp::Pinned<ShiftTrackerCase::Tracker, ShiftTrackerCase::Tracker::Mode::REMOVE>;
/// @brief The delay's feedback chain, as the delay processor builds it: shaped, then high-passed.
using DelayFeedbackChain = dsp::Chain<dsp::fixed::FeedbackShaper, DelayHighPass>;

/// @brief The delay feedback cases' feedback amount, Q31: a half.
inline constexpr q31 kDelayFeedback = 1 << 30;
/// @brief The delay feedback cases' tanh drive, as a power of two: a sound's.
inline constexpr uint32_t kDelayDrive = 8;

/// @brief The delay feedback cases' settings for one mode: kDelayFeedback at kDelayDrive.
/// @param analog Through the impulse response and a tanh; else clipped digitally.
[[nodiscard]] constexpr dsp::fixed::FeedbackShaper::Settings delay_feedback_settings(bool analog) {
	return {.feedback = kDelayFeedback, .analog = analog, .drive = kDelayDrive};
}

/// @brief Where a delay feedback case renders its blocks.
enum class Placement : uint8_t {
	/// In the lanes output, as the delay renders the owner's block in place: setup copies the lanes input's first n
	/// frames there,
	/// and each block renders the block before's output.
	IN_PLACE,
	/// From the lanes input's first n frames into the lanes output, every block.
	OUT_OF_PLACE,
};

/// @brief The delay's feedback stages as two passes over the block, each in place in the lanes output:
/// `Stereo<FeedbackShaper>`, then `Stereo<DelayHighPass>`.
/// @details Setup copies the lanes input's first n frames into the lanes output, and every block renders the block
/// before's output, as the delay renders the owner's block (Placement::IN_PLACE).
///
/// **Timed state:** the response's history (analog) and each channel's high-pass memory carry from block to block,
/// from silence at setup, and each block's input is the block before's output. Every frame is the same arithmetic,
/// whatever the samples: in ANALOG the 26-tap response, the feedback multiply and the tanh at the drive; in DIGITAL
/// the multiply and the 29-bit clip; then the high-pass's subtract, shifted add and subtract.
/// @tparam Analog The shaper's mode: true for `delay_feedback_passes_analog`, false for `_digital`.
template <bool Analog>
class DelayFeedbackPassesCase {
public:
	static constexpr std::size_t kLanes = 2; ///< Left and right.

	/// @brief Configures both passes from silence, fills the lanes input, copies its first @p n frames into the lanes
	/// output and takes that block's span.
	void setup(std::size_t n);
	/// @brief Processes one block of the size it was set up for, in place: the shaper's pass, then the high-pass's.
	void block(std::size_t n);
	/// @return The shaper's component.
	[[nodiscard]] const dsp::Stereo<dsp::fixed::FeedbackShaper>& shaper() const { return shaper_; }
	/// @return The high-pass's component.
	[[nodiscard]] const dsp::Stereo<DelayHighPass>& high_pass() const { return high_pass_; }

private:
	dsp::Stereo<dsp::fixed::FeedbackShaper> shaper_;
	dsp::Stereo<DelayHighPass> high_pass_;
	dsp::Buffer<StereoSample> work_{}; ///< The lanes output's first n frames, which both passes render in place.
};

/// @brief `delay_feedback_passes_analog`.
using DelayFeedbackPassesAnalogCase = DelayFeedbackPassesCase<true>;
/// @brief `delay_feedback_passes_digital`.
using DelayFeedbackPassesDigitalCase = DelayFeedbackPassesCase<false>;

/// @brief The delay's feedback as one pass of `Stereo<DelayFeedbackChain>`.
/// @details In place (Placement::IN_PLACE) it is the delay's own render, `renderBlock(work, work)`: the loop the delay
/// ships, whose one frame pointer is stepped after the frame's store. Out of place it renders the lanes input into the
/// lanes output, a loop with an input pointer and an output pointer; `delay_feedback_chain_digital_out_of_place` times
/// that form against the in-place one.
///
/// **Timed state:** as DelayFeedbackPassesCase's, the same arithmetic per frame in one pass; out of place, every block
/// reads the same input.
/// @tparam Analog The shaper's mode: true for `delay_feedback_chain_analog`, false for `_digital`.
/// @tparam P Where it renders.
template <bool Analog, Placement P = Placement::IN_PLACE>
class DelayFeedbackChainCase {
public:
	static constexpr std::size_t kLanes = 2; ///< Left and right.

	/// @brief Configures the chain from silence, fills the lanes input and takes the blocks' spans for @p n frames; in
	/// place, copies the input's frames into the lanes output and takes that block's span alone.
	void setup(std::size_t n);
	/// @brief Processes one block of the size it was set up for.
	void block(std::size_t n);
	/// @return The component.
	[[nodiscard]] const dsp::Stereo<DelayFeedbackChain>& component() const { return chain_; }

private:
	dsp::Stereo<DelayFeedbackChain> chain_;
	dsp::Buffer<StereoSample> work_{}; ///< In place: the lanes output's first n frames, passed as input and output.
	FrameSpans<StereoSample> spans_{}; ///< Out of place: the lanes blocks' first n frames.
};

/// @brief `delay_feedback_chain_analog`.
using DelayFeedbackChainAnalogCase = DelayFeedbackChainCase<true>;
/// @brief `delay_feedback_chain_digital`.
using DelayFeedbackChainDigitalCase = DelayFeedbackChainCase<false>;
/// @brief `delay_feedback_chain_digital_out_of_place`.
using DelayFeedbackChainDigitalOutOfPlaceCase = DelayFeedbackChainCase<false, Placement::OUT_OF_PLACE>;

/// @brief The delay's analog feedback with the response stepping both channels in one tap loop: the stereo reference
/// response, then a tanh pass and a high-pass pass, each a plain loop over both channels in place in the lanes
/// output, as the delay ran them.
/// @details The baseline the chain's analog cost is measured against: its output is bit for bit
/// `delay_feedback_chain_analog`'s, so the two time the same work in two forms, and the difference is the per-channel
/// response's tap loop against the stereo one, less the two passes the chain fuses. Its response's adds wrap, as the
/// target's `add` computes them, since in place a block's input is the block before's output, which can be near full
/// scale. The drive is read from a member, as the processor read it from its settings, so the tanh's shift stays a
/// run-time one.
///
/// **Timed state:** as DelayFeedbackPassesCase's in ANALOG.
class DelayFeedbackLegacyAnalogCase {
public:
	static constexpr std::size_t kLanes = 2; ///< Left and right.

	/// @brief Clears the response and the memories, fills the lanes input, copies its first @p n frames into the lanes
	/// output and takes that block's span.
	void setup(std::size_t n);
	/// @brief Processes one block of the size it was set up for, in place: the response's pass, the tanh's and the
	/// high-pass's.
	void block(std::size_t n);
	/// @return The left high-pass memory, Q31.
	[[nodiscard]] int32_t high_pass_l() const { return high_pass_l_; }
	/// @return The right high-pass memory, Q31.
	[[nodiscard]] int32_t high_pass_r() const { return high_pass_r_; }
	/// @return The feedback amount, Q31.
	[[nodiscard]] q31 feedback() const { return feedback_; }
	/// @return The tanh drive, as a power of two.
	[[nodiscard]] uint32_t drive() const { return drive_; }

private:
	reference::WrappingReferenceImpulseResponse response_;
	int32_t high_pass_l_ = 0;
	int32_t high_pass_r_ = 0;
	q31 feedback_ = kDelayFeedback;
	uint32_t drive_ = kDelayDrive;
	std::span<StereoSample> work_{};
};

/// @brief The chain3 cases' low-pass, pinned: the one-pole at the granular moveability.
using Chain3LowPass = dsp::Pinned<dsp::fixed::OnePole, dsp::fixed::OnePole::Mode::LOW_PASS>;
/// @brief Three stages as one kernel: tanh at drive 3, the low-pass, then the delay's high-pass.
using Chain3 = dsp::Chain<Tanh3Case::Kernel, Chain3LowPass, DelayHighPass>;

/// @brief Tanh at drive 3, the one-pole low-pass and the delay's high-pass as three `Mono` passes: the first from
/// input_a into the output, the others over the output in place.
/// @details **Timed state:** every block reads the same input; the low-pass's and the high-pass's memories carry from
/// block to block, from silence at setup. Every sample is the same arithmetic: the tanh's table read, the low-pass's
/// subtract, rounding multiply and two shifted adds, and the high-pass's subtract, shifted add and subtract.
class Chain3PassesCase {
public:
	/// @brief Configures the stages from silence, fills input_a and takes the blocks' spans for @p n.
	void setup(std::size_t n);
	/// @brief Processes one block of the size it was set up for: each stage's pass in turn.
	void block(std::size_t n);
	/// @return The low-pass's component.
	[[nodiscard]] const dsp::Mono<Chain3LowPass>& low_pass() const { return low_pass_; }
	/// @return The high-pass's component.
	[[nodiscard]] const dsp::Mono<DelayHighPass>& high_pass() const { return high_pass_; }

private:
	dsp::Mono<Tanh3Case::Kernel> tanh_;
	dsp::Mono<Chain3LowPass> low_pass_;
	dsp::Mono<DelayHighPass> high_pass_;
	BlockSpans spans_{};
	dsp::fixed::Signal written_{}; ///< The output's first n samples, read by the later passes.
};

/// @brief `Mono<Chain3>`: the three stages in one pass from input_a into the output.
/// @details **Timed state:** as Chain3PassesCase's, the same arithmetic per sample in one pass.
class Chain3FusedCase {
public:
	/// @brief Configures the chain from silence, fills input_a and takes the blocks' spans for @p n.
	void setup(std::size_t n);
	/// @brief Processes one block of the size it was set up for.
	void block(std::size_t n);
	/// @return The component.
	[[nodiscard]] const dsp::Mono<Chain3>& component() const { return chain_; }

private:
	dsp::Mono<Chain3> chain_;
	BlockSpans spans_{};
};

/// @brief How a delay whole case's primary tape plays.
enum class DelayTapeForm : uint8_t {
	NATIVE, ///< At its native rate: one step per sample, the write kWriteLag behind the read.
	RES_LO, ///< Resampled at kDelayResLoRate, below its native rate: the slowed-down four-sample write.
	RES_HI, ///< Resampled at kDelayResHiRate, above its native rate: the sped-up triangle write.
};

/// @brief The delay's neutral rate (8.24 of the neutral tape speed): a tape of DelayTape::kNeutralSize samples, the
/// primary tape's native rate in every delay whole case.
inline constexpr int32_t kDelayNeutralRate = 16'777'216;
/// @brief The res_lo cases' rate: three quarters of the native rate, between half of it (below which the owner grows a
/// new tape at once) and the native rate.
inline constexpr int32_t kDelayResLoRate = kDelayNeutralRate / 4 * 3;
/// @brief The res_hi and growing cases' rate: one and a half times the native rate, below double it (at which the owner
/// grows a new tape at once).
inline constexpr int32_t kDelayResHiRate = kDelayNeutralRate / 2 * 3;
/// @brief The storage each delay tape is given, in samples: the largest the app allocates, kMaxSize plus the write's
/// run-off.
inline constexpr std::size_t kDelayStorageSize =
    dsp::fixed::DelayTape::kMaxSize + static_cast<std::size_t>(dsp::fixed::DelayTape::kWriteLag);

/// @brief One delay whole case's configuration.
struct DelayWholeConfig {
	DelayTapeForm tape; ///< How the primary tape plays.
	bool cross;         ///< Ping-pong in a stereo render, so the feedback crosses; else straight.
	bool analog;        ///< The analog shaper (impulse response and tanh); else the digital clip.
	bool growing;       ///< A secondary tape grows alongside, as the owner grows one while the rate settles.
};

/// @brief The block settings a delay whole case renders every block with: kDelayFeedback, a sound's drive
/// (kDelayDrive), in a stereo render.
/// @param config The case's configuration.
[[nodiscard]] constexpr dsp::fixed::DelayBlock delay_whole_block(const DelayWholeConfig& config) {
	const int32_t rate = config.tape == DelayTapeForm::RES_LO   ? kDelayResLoRate
	                     : config.tape == DelayTapeForm::RES_HI ? kDelayResHiRate
	                                                            : kDelayNeutralRate;
	return {.rate = rate,
	        .feedback = kDelayFeedback,
	        .analog = config.analog,
	        .analog_saturation = static_cast<int32_t>(kDelayDrive),
	        .ping_pong = config.cross,
	        .render_in_stereo = true};
}

/// @brief Fills a tape's storage with stereo noise at 2^-10 of full scale: a block's sum of its own input and every
/// delayed sample the case's run adds to it stays inside q31.
void fill_delay_tape(std::span<StereoSample> storage);

/// @brief What every delay whole case renders with: one processor and one pair of tapes, over the two storage blocks
/// in SDRAM, as the app allocates its delay tapes. The cases run one after another, so they share it; each case's
/// setup builds it afresh, so it fits on-chip SRAM beside every other case.
struct DelayRig {
	dsp::fixed::DelayProcessor processor;        ///< The delay's block, as a Sound's Delay holds it.
	dsp::fixed::DelayTape primary;               ///< The tape read and written back.
	dsp::fixed::DelayTape secondary;             ///< The tape grown alongside; inactive unless the case grows one.
	dsp::fixed::DelayTape* growing = nullptr;    ///< &secondary when the case grows one, else null.
	dsp::fixed::DelayBlock settings{};           ///< The case's block settings.
	dsp::fixed::DelayBlockResult result{};       ///< The last block's result.
	dsp::Buffer<StereoSample> work{};            ///< The lanes output's first n frames, rendered in place.
	std::span<StereoSample> primary_storage{};   ///< The primary's storage, kDelayStorageSize samples in SDRAM.
	std::span<StereoSample> secondary_storage{}; ///< The secondary's storage, kDelayStorageSize samples in SDRAM.
};

/// @return The rig the delay whole cases render with.
[[nodiscard]] const DelayRig& delay_rig();

/// @brief The whole delay block, `DelayProcessor::process`, over real tapes, as a Sound's Delay renders it: one fused
/// frame loop that reads the tape, shapes and high-passes the feedback, mixes and writes the tapes, frame by frame.
/// @details The primary tape is the neutral rate's (DelayTape::kNeutralSize samples, native at kDelayNeutralRate),
/// filled with fill_delay_tape(), and plays at the case's rate. Growing, a secondary tape sized for kDelayResHiRate is
/// grown alongside, native there, zeroed, the primary's native rate made precise relative to it: the owner's tapes in
/// the block after its rate settled at kDelayResHiRate. The block is the lanes output's first n frames, a quarter of
/// the lanes input copied there at setup, rendered in place, as the delay renders its owner's block.
///
/// **Timed state:** the feedback chain from silence at setup; the read head walks the noise the tape was filled with
/// (no run reaches the end of a tape, so nothing it wrote is read back), and every frame is the same work for the
/// tape's form: in NATIVE one step, one read and one write; in RES_LO and RES_HI the long position's step, the
/// interpolated read and the triangle write (four samples slowed down, about six sped up at 1.5); between them the
/// feedback (the 26-tap response and the tanh in ANALOG, the clip in DIGITAL), the high-pass and the mix (crossed or
/// straight). Growing adds the secondary's native write, clearing ahead of its head, which never wraps in a run.
///
/// One class serves every configuration, which each case holds as data, so the image carries one setup and one block
/// for all of them.
class DelayWholeCase {
public:
	static constexpr std::size_t kLanes = 2; ///< Left and right.

	/// @brief A case of @p config.
	constexpr explicit DelayWholeCase(DelayWholeConfig config) : config_{config} {}

	/// @brief Builds the rig for @p n frames: a fresh processor, the tapes and the block.
	void setup(std::size_t n);
	/// @brief Renders one block of the size it was set up for, in place.
	void block(std::size_t n);
	/// @return The case's configuration.
	[[nodiscard]] constexpr const DelayWholeConfig& config() const { return config_; }

private:
	DelayWholeConfig config_;
};

/// @brief The configuration of `delay_whole_<tape>_<mix>_<shaper>`, with no secondary.
[[nodiscard]] constexpr DelayWholeConfig delay_whole_mode(DelayTapeForm tape, bool cross, bool analog) {
	return {.tape = tape, .cross = cross, .analog = analog, .growing = false};
}
/// @brief The configuration of `delay_whole_growing`: res_hi, straight, digital, with a secondary growing.
inline constexpr DelayWholeConfig kDelayWholeGrowing{
    .tape = DelayTapeForm::RES_HI, .cross = false, .analog = false, .growing = true};

/// @brief The filter cases' seed for the synth-wide noise generator (util/waves.h's jcong), which the low-pass ladder
/// draws from: the app's boot value.
inline constexpr uint32_t kFilterNoiseSeed = 380'116'160;
/// @brief The gain going into every filter case's set, Q31: a quarter.
inline constexpr q31 kFilterGain = 536'870'912;

/// @brief The low-pass settings the filter cases run at: the cutoff at 2^23 (Q31), half resonance (2^28 of Q29's
/// 2^29), no morph. The drive ladder does not oversample here: the cutoff's quick_log is 50 · 2^24, below the 52 at
/// which oversampling starts (2^24 would reach it, and oversample at this resonance). The resonance is high enough that
/// the ladders saturate their input.
inline constexpr dsp::fixed::FilterConfig kFilterLowSettings{
    .frequency = 1 << 23, .resonance = 1 << 28, .morph = 0, .allow_oversampling = true};
/// @brief The drive ladder's oversampling settings: a bright cutoff (50,000,000, Q31) at high resonance (500,000,000,
/// Q29), past the threshold above which the drive ladder runs at twice the rate.
inline constexpr dsp::fixed::FilterConfig kFilterBrightSettings{
    .frequency = 50'000'000, .resonance = 500'000'000, .morph = 0, .allow_oversampling = true};
/// @brief The high-pass settings the filter cases run at: the cutoff at 2^23 (Q31), a quarter resonance (2^27 of Q29's
/// 2^29), no morph. The ladder's processed resonance stays below the level at which it saturates: the high-pass
/// ladder's unsaturated path, where most sounds run it.
inline constexpr dsp::fixed::FilterConfig kFilterHighSettings{
    .frequency = 1 << 23, .resonance = 1 << 27, .morph = 0, .allow_oversampling = true};

/// @brief One filter case's configuration: what each slot of the set runs, the route, and each filter's settings.
struct FilterCaseConfig {
	dsp::fixed::LowPassSlot low;          ///< What the low-pass slot runs.
	dsp::fixed::HighPassSlot high;        ///< What the high-pass slot runs.
	dsp::fixed::FilterRoute route;        ///< The order the two run in.
	dsp::fixed::FilterConfig low_config;  ///< The low-pass's settings.
	dsp::fixed::FilterConfig high_config; ///< The high-pass's settings.
	/// Each block switches the low-pass off and back on before it filters, so the low-pass fades in from dry over the
	/// whole block.
	bool fading = false;
};

/// @brief The configuration of a case of the low-pass slot alone, running @p low at @p settings.
[[nodiscard]] constexpr FilterCaseConfig
filter_low_case(dsp::fixed::LowPassSlot low, const dsp::fixed::FilterConfig& settings = kFilterLowSettings) {
	return {.low = low,
	        .high = {},
	        .route = dsp::fixed::FilterRoute::HIGH_TO_LOW,
	        .low_config = settings,
	        .high_config = kFilterHighSettings};
}

/// @brief `filter_lp12_*`: the 12 dB ladder.
inline constexpr FilterCaseConfig kFilterLow12 = filter_low_case(
    {.kind = dsp::fixed::LowPassSlot::Kind::LADDER, .ladder = dsp::fixed::LowPassLadderMode::TRANSISTOR_12DB});
/// @brief `filter_lp24_*`: the 24 dB ladder.
inline constexpr FilterCaseConfig kFilterLow24 = filter_low_case(
    {.kind = dsp::fixed::LowPassSlot::Kind::LADDER, .ladder = dsp::fixed::LowPassLadderMode::TRANSISTOR_24DB});
/// @brief `filter_lpdrive_*`: the drive ladder at the rate of the block.
inline constexpr FilterCaseConfig kFilterLowDrive = filter_low_case(
    {.kind = dsp::fixed::LowPassSlot::Kind::LADDER, .ladder = dsp::fixed::LowPassLadderMode::TRANSISTOR_24DB_DRIVE});
/// @brief `filter_lpdrive_os_*`: the drive ladder oversampled, at kFilterBrightSettings.
inline constexpr FilterCaseConfig kFilterLowDriveOs = filter_low_case(
    {.kind = dsp::fixed::LowPassSlot::Kind::LADDER, .ladder = dsp::fixed::LowPassLadderMode::TRANSISTOR_24DB_DRIVE},
    kFilterBrightSettings);
/// @brief `filter_svf_band_*`: the SVF in band mode, in the low-pass slot.
inline constexpr FilterCaseConfig kFilterSvfBand =
    filter_low_case({.kind = dsp::fixed::LowPassSlot::Kind::SVF, .svf = dsp::fixed::SvfMode::BAND});
/// @brief `filter_svf_notch_*`: the SVF in notch mode, in the low-pass slot.
inline constexpr FilterCaseConfig kFilterSvfNotch =
    filter_low_case({.kind = dsp::fixed::LowPassSlot::Kind::SVF, .svf = dsp::fixed::SvfMode::NOTCH});
/// @brief `filter_hp_*`: the high-pass ladder alone.
inline constexpr FilterCaseConfig kFilterHigh{.low = {},
                                              .high = {.kind = dsp::fixed::HighPassSlot::Kind::LADDER},
                                              .route = dsp::fixed::FilterRoute::HIGH_TO_LOW,
                                              .low_config = kFilterLowSettings,
                                              .high_config = kFilterHighSettings};

/// @brief The configuration of `filterset_<hl|lh|par>_stereo`: the 24 dB ladder and the high-pass ladder (a sound's
/// default modes) on @p route.
[[nodiscard]] constexpr FilterCaseConfig filter_set_case(dsp::fixed::FilterRoute route) {
	return {.low = kFilterLow24.low,
	        .high = kFilterHigh.high,
	        .route = route,
	        .low_config = kFilterLowSettings,
	        .high_config = kFilterHighSettings};
}

/// @brief `filter_lp24_fading_stereo`: the 24 dB ladder fading in over every block. Against `filter_lp24_stereo`, it
/// times the fade pass while it is active: the dry copy and the blend, with the level stepped per channel-sample.
inline constexpr FilterCaseConfig kFilterLow24Fading = [] {
	FilterCaseConfig config = kFilterLow24;
	config.fading = true;
	return config;
}();

/// @brief Fills the lanes input with noise at 2^-4 of full scale, about an oscillator's level going into a voice's
/// filters.
void fill_filter_input();

/// @brief Fills the lanes input (fill_filter_input()) and copies its first @p samples samples into the lanes output.
/// @return Those output samples: the block a filter case renders first, in place.
[[nodiscard]] std::span<q31> in_place_filter_block(std::size_t samples);

/// @brief What every filter case renders with: one filter set, as a voice holds it. The cases run one after another,
/// so they share it; each case's setup resets it.
struct FilterRig {
	dsp::fixed::FilterSet set; ///< The set, as a Voice's filterSet.
	q31 gain{};                ///< The last configure's gain.
	std::span<q31> work{};     ///< The lanes output's first n samples (2n in stereo), filtered in place.
};

/// @return The rig the filter cases render with.
[[nodiscard]] const FilterRig& filter_rig();

/// @brief A filter set's block as a voice renders it, `FilterSet::configure` then `renderMono` or `renderStereo`, in
/// place over the lanes output, for a configuration the case holds as data.
/// @details Setup resets the set, as a new voice's, and seeds the synth-wide noise at kFilterNoiseSeed; the block is
/// the lanes input's first n samples (2n interleaved in stereo, as fill_filter_input() fills it) copied into the lanes
/// output. Each block configures the set (the voice configures it every block) and filters the block in place, over
/// the block before's output.
///
/// **Timed state:** every slot that runs holds its filter from the first block (a reset set holds ladders, and an SVF
/// takes over the slot without a fade), so no fade runs and every block is the steady state of its filters: the
/// low-pass ladder drawing one noise sample per channel-sample (two when the drive ladder oversamples), the high-pass
/// ladder on its unsaturated path, the SVF's two passes per sample, and in the parallel route the copy of the block
/// and the wrapping sum. A fading configuration (FilterCaseConfig::fading) is the exception: each block first
/// configures the low-pass off, then on, which resets the ladder's sections and restarts its fade, so every block
/// runs the fade pass from its start (the level's arithmetic costs the same at every level).
/// @tparam Stereo Interleaved L/R frames, `renderStereo`; else mono, `renderMono`.
template <bool Stereo>
class FilterCase {
public:
	static constexpr std::size_t kLanes = Stereo ? 2 : 1; ///< A frame's samples.

	/// @brief A case of @p config.
	constexpr explicit FilterCase(const FilterCaseConfig& config) : config_{config} {}

	/// @brief Resets the set, seeds the noise and fills the block for @p n frames.
	void setup(std::size_t n);
	/// @brief Configures the set and filters one block of the size it was set up for, in place.
	void block(std::size_t n);
	/// @return The case's configuration.
	[[nodiscard]] constexpr const FilterCaseConfig& config() const { return config_; }

private:
	FilterCaseConfig config_;
};

/// @brief A filter case over mono blocks.
using FilterMonoCase = FilterCase<false>;
/// @brief A filter case over interleaved stereo blocks.
using FilterStereoCase = FilterCase<true>;

/// @brief The compressor cases' threshold knob, Q31 of its travel: half, where the compressor reduces the cases' level.
inline constexpr q31 kCompressorThreshold = 1 << 30;
/// @brief The blended cases' blend knob, Q31: half, which the menu shows as 50%.
inline constexpr q31 kCompressorHalfBlend = 1 << 30;
/// @brief The channels' gain in the `render` cases, 4.27 signed: the song master's neutral post-FX volume
/// (167,763,968), halved as the master halves it.
inline constexpr q31 kCompressorVolume = 83'881'984;
/// @brief The final volume, 3.29: GLOBAL_VOLUME_POST_FX's neutral value, as a sound passes it to
/// `renderVolumeNeutral`. The `render` cases pass it shifted down by 3, as `renderVolumeNeutral` does.
inline constexpr q31 kCompressorFinalVolume = 134'217'728;

/// @brief One compressor case's configuration.
struct CompressorCaseConfig {
	q31 blend;    ///< The blend knob, Q31: ONE_Q31 fully wet.
	bool neutral; ///< `renderVolumeNeutral`, as a sound and a clip chain call it; else `render`, as the song master
	              ///< does.
};

/// @brief `compressor_wet_render`: fully wet, through `render`.
inline constexpr CompressorCaseConfig kCompressorWetRender{.blend = ONE_Q31, .neutral = false};
/// @brief `compressor_wet_neutral`: fully wet, through `renderVolumeNeutral`.
inline constexpr CompressorCaseConfig kCompressorWetNeutral{.blend = ONE_Q31, .neutral = true};
/// @brief `compressor_blend_render`: half blended, through `render`.
inline constexpr CompressorCaseConfig kCompressorBlendRender{.blend = kCompressorHalfBlend, .neutral = false};
/// @brief `compressor_blend_neutral`: half blended, through `renderVolumeNeutral`.
inline constexpr CompressorCaseConfig kCompressorBlendNeutral{.blend = kCompressorHalfBlend, .neutral = true};

/// @return The knobs a compressor case configures: the defaults, with kCompressorThreshold and @p config's blend.
[[nodiscard]] constexpr dsp::fixed::CompressorKnobs compressor_knobs(const CompressorCaseConfig& config) {
	return {.threshold = kCompressorThreshold, .blend = config.blend};
}

/// @brief Fills the lanes input with noise at 2^-3 of full scale: loud, yet a blended block's level sum stays inside
/// q31.
void fill_compressor_input();

/// @brief What every compressor case renders with: one compressor, as a sound holds it. The cases run one after
/// another, so they share it; each case's setup replaces it with a fresh one.
struct CompressorRig {
	dsp::fixed::RmsFeedbackCompressor compressor; ///< The compressor, as a Sound's.
	dsp::Buffer<StereoSample> work{};             ///< The lanes output's first n frames, compressed in place.
};

/// @return The rig the compressor cases render with.
[[nodiscard]] const CompressorRig& compressor_rig();

/// @brief The compressor's block as its owners render it today: `RmsFeedbackCompressor::render` (the song master) or
/// `renderVolumeNeutral` (a sound, a clip chain), in place over the lanes output, fully wet or blended.
/// @details Setup replaces the compressor with a fresh one at compressor_knobs(), and copies the lanes input's first n
/// frames (fill_compressor_input()) into the lanes output. Each block compresses that block in place, over the block
/// before's output.
///
/// **Timed state:** from the second block the output is the saturation's, within ±2^26, plus in the blended cases half
/// the block before's: every block ramps both gains, saturates both channels, blends when below fully wet (reading each
/// input frame in place), then measures the level through the detector. The level is past the threshold, so the
/// compressor reduces the gain, and the level's sum never wraps.
///
/// One class serves every configuration, which each case holds as data, so the image carries one setup and one block
/// for all of them.
class CompressorCase {
public:
	static constexpr std::size_t kLanes = 2; ///< Left and right.

	/// @brief A case of @p config.
	constexpr explicit CompressorCase(const CompressorCaseConfig& config) : config_{config} {}

	/// @brief A fresh compressor at the cases' knobs, and the block for @p n frames.
	void setup(std::size_t n);
	/// @brief Compresses one block of the size it was set up for, in place.
	void block(std::size_t n);
	/// @return The case's configuration.
	[[nodiscard]] constexpr const CompressorCaseConfig& config() const { return config_; }

private:
	CompressorCaseConfig config_;
};

/// @brief The mod FX cases' LFO rate: a fast LFO, UQ0.32-style phase increment, so the delay tap moves within a block.
inline constexpr int32_t kModFxRate = 0x00a00000;
/// @brief The mod FX cases' depth knob, Q31 of its travel: half.
inline constexpr int32_t kModFxDepth = 0x40000000;
/// @brief The mod FX cases' offset and feedback params, Q31 of their travel: a quarter.
inline constexpr int32_t kModFxParam = 0x20000000;

/// @brief The mod FX effects the cases time. mod_fx_cases.cpp maps each to the app's `ModFXType`, which this header
/// leaves out so that the host specs build it without the app's headers.
enum class ModFxKind : uint8_t {
	CHORUS,        ///< `ModFXType::CHORUS`
	CHORUS_STEREO, ///< `ModFXType::CHORUS_STEREO`
	DIMENSION,     ///< `ModFXType::DIMENSION`
	FLANGER,       ///< `ModFXType::FLANGER`
	PHASER,        ///< `ModFXType::PHASER`
	WARBLE,        ///< `ModFXType::WARBLE`
};

/// @brief One mod FX case's configuration.
struct ModFxConfig {
	ModFxKind kind; ///< The effect.
	bool stereo;    ///< The stereo flag the owner hands the component, as `AudioEngine::renderInStereo` sets it.
};

// Each effect with the engine rendering in stereo and in mono, named for its case.
inline constexpr ModFxConfig kModFxChorusStereo{.kind = ModFxKind::CHORUS, .stereo = true};
inline constexpr ModFxConfig kModFxChorusMono{.kind = ModFxKind::CHORUS, .stereo = false};
inline constexpr ModFxConfig kModFxChorusStereoStereo{.kind = ModFxKind::CHORUS_STEREO, .stereo = true};
inline constexpr ModFxConfig kModFxChorusStereoMono{.kind = ModFxKind::CHORUS_STEREO, .stereo = false};
inline constexpr ModFxConfig kModFxDimensionStereo{.kind = ModFxKind::DIMENSION, .stereo = true};
inline constexpr ModFxConfig kModFxDimensionMono{.kind = ModFxKind::DIMENSION, .stereo = false};
inline constexpr ModFxConfig kModFxFlangerStereo{.kind = ModFxKind::FLANGER, .stereo = true};
inline constexpr ModFxConfig kModFxFlangerMono{.kind = ModFxKind::FLANGER, .stereo = false};
inline constexpr ModFxConfig kModFxWarbleStereo{.kind = ModFxKind::WARBLE, .stereo = true};
inline constexpr ModFxConfig kModFxWarbleMono{.kind = ModFxKind::WARBLE, .stereo = false};
/// @brief `mod_fx_phaser`: the phaser ignores the stereo flag.
inline constexpr ModFxConfig kModFxPhaser{.kind = ModFxKind::PHASER, .stereo = true};

/// @brief The mod FX block as its owner renders it: `effects::ModFx::render` in place over the lanes output, in the
/// configuration's stereo mode.
/// @details Setup makes the component anew (its LFOs at rest, and a zeroed ring taken through prepare, commit and
/// finish for the effects that run over one), restores the volume and copies the lanes input's first n frames
/// (fill_compressor_input()'s sounding block) into the lanes output. Each block processes that block in place, over
/// the block before's output, so the signal is not a steady tone after the first blocks; the timing does not depend
/// on the data.
class ModFxCase {
public:
	static constexpr std::size_t kLanes = 2; ///< Left and right.

	/// @brief A case of @p config.
	constexpr explicit ModFxCase(const ModFxConfig& config) : config_{config} {}

	/// @brief A fresh component at the cases' knobs, and the block for @p n frames.
	void setup(std::size_t n);
	/// @brief Processes one block of the size it was set up for, in place.
	void block(std::size_t n);
	/// @return The case's configuration.
	[[nodiscard]] constexpr const ModFxConfig& config() const { return config_; }

private:
	ModFxConfig config_;
};

/// @brief The granular cases' rate knob, a signed Q31 param value: 0x10000 maps (on the kernel's prepare's log curve)
/// to about 71 grain starts per second at 44.1 kHz, one every 1,242 frames, a mid setting of the knob's range. It
/// differs from the kernel's initial knob position (0), so the first call applies it.
inline constexpr int32_t kGranularRate = 0x10000;
/// @brief The granular cases' mix knob, signed Q31: 0x40000000 gives the cubic mix curve a wet volume of 0.875
/// (0x70000000) and a full dry volume. It differs from the kernel's initial knob position (0), so the first call
/// applies it; at 0 the wet volume would stay at the constructor's silence.
inline constexpr int32_t kGranularMix = 0x40000000;
/// @brief The sparse cases' density knob, signed Q31: grains of about 3,000 frames (the kernel's prepare scales the
/// length by the knob's 0..1 position, here 1/8), so two or three of the eight grains sound at a time.
inline constexpr int32_t kGranularDensityLow = -0x60000000;
/// @brief The dense cases' density knob, signed Q31: grains of about 10,600 frames (position 7/8), so all eight
/// grains sound nearly all the time.
inline constexpr int32_t kGranularDensityHigh = 0x60000000;
/// @brief The unison cases' pitch randomness, signed Q31: none, so every grain is at the original pitch (some play
/// reversed).
inline constexpr int32_t kGranularRandomnessNone = 0;
/// @brief The random cases' pitch randomness, signed Q31: the maximum, so grains draw their pitch from the full
/// range.
inline constexpr int32_t kGranularRandomnessMax = 0x7fffffff;
/// @brief The granular cases' reverb send knob, Q31: a moderate amount. Each block turns it into the component's
/// argument with the post-effect volume, as ModControllableAudio::processGrainFX does.
inline constexpr int32_t kGranularVerbAmount = 0x10000000;
/// @brief The frames of untimed setup blocks: more than one full 65,536-frame ring. The kernel wraps its write index
/// lazily, inside the per-sample step, and sets its initialised state only when a call starts with the index exactly at
/// 65,536; that happens when the block length divides 65,536 (the bench's 32, 64 and 128 do). From then on grains draw
/// random pitches and start points, the steady state, and the ring is more than half full, so grains start.
inline constexpr std::size_t kGranularFillFrames = 66'000;

/// @brief One granular case's configuration.
struct GranularConfig {
	int32_t density;    ///< The density knob (signed Q31).
	int32_t randomness; ///< The pitch randomness knob (signed Q31).
};

// Each density with each pitch randomness, named for its case.
inline constexpr GranularConfig kGranularSparseUnison{.density = kGranularDensityLow,
                                                      .randomness = kGranularRandomnessNone};
inline constexpr GranularConfig kGranularSparseRandom{.density = kGranularDensityLow,
                                                      .randomness = kGranularRandomnessMax};
inline constexpr GranularConfig kGranularDenseUnison{.density = kGranularDensityHigh,
                                                     .randomness = kGranularRandomnessNone};
inline constexpr GranularConfig kGranularDenseRandom{.density = kGranularDensityHigh,
                                                     .randomness = kGranularRandomnessMax};

/// @brief The granular block as its owner renders it: `effects::Granular::render` in place over the lanes output, with
/// the reverb feed going to the rig's send buffer.
/// @details Setup makes the component anew, takes its State and ring, and reseeds the noise generator, then runs
/// untimed blocks past one full ring, so grains are live and initialised before the timed region. Each block processes
/// the lanes input's sounding block in place, as the mod FX cases do.
class GranularCase {
public:
	static constexpr std::size_t kLanes = 2; ///< Left and right.

	/// @brief A case of @p config.
	constexpr explicit GranularCase(const GranularConfig& config) : config_{config} {}

	/// @brief A fresh component run past one full ring, and the block for @p n frames.
	void setup(std::size_t n);
	/// @brief Processes one block of the size it was set up for, in place.
	void block(std::size_t n);
	/// @return The case's configuration.
	[[nodiscard]] constexpr const GranularConfig& config() const { return config_; }

private:
	GranularConfig config_;
};

/// @brief The stutter cases' tick magnitude and tick inverse: a song at its default resolution near 120 bpm (229.7
/// samples a tick), which gives the knob's middle a tape of about 900 frames.
inline constexpr int32_t kStutterMagnitude = 1;
inline constexpr uint32_t kStutterTickInverse = 18'700'000;
/// @brief The stutter-rate param (signed Q31) a recording case begins at: its slowest, the largest tape.
inline constexpr int32_t kStutterRateSlowest = -0x7fffffff;
/// @brief The param a playing case begins at: a fast rate, so its short tape records untimed.
inline constexpr int32_t kStutterRateFast = 0x30000000;
/// @brief How far a resampled case moves the param off the tape's native rate.
inline constexpr int32_t kStutterRateNudge = 0x08000000;
/// @brief Frames a playing case records untimed: past any tape kStutterRateFast makes.
inline constexpr std::size_t kStutterRecordFrames = 20'000;

/// @brief One stutter case's configuration.
struct StutterConfig {
	bool play = false;      ///< Played back; otherwise recording.
	bool resampled = false; ///< The rate moved off the tape's native rate.
};

/// @brief The stutter block as its owner renders it, in place over the lanes output.
/// @details Setup begins a stutter anew as the gold knob's press begins one. A playing case records its tape untimed
/// first; a resampled case then moves the rate off the tape's native one.
class StutterCase {
public:
	static constexpr std::size_t kLanes = 2; ///< Left and right.

	/// @brief A case of @p config.
	constexpr explicit StutterCase(const StutterConfig& config) : config_{config} {}

	/// @brief A stutter begun anew, and the block for @p n frames.
	void setup(std::size_t n);
	/// @brief Processes one block of the size it was set up for, in place.
	void block(std::size_t n);

private:
	StutterConfig config_;
};

/// @brief Every case, by name.
struct CaseSet {
	CalibrationLoop calibration_loop;                             ///< `calibration_loop`
	AmplitudeCase amplitude;                                      ///< `amplitude`
	CrudeAmplitudeCase crude_amplitude;                           ///< `crude_amplitude`
	PulseCase pulse;                                              ///< `pulse`
	BandLimitedTableCase band_limited_table;                      ///< `band_limited_table`
	CrudeSawCase crude_saw;                                       ///< `crude_saw`
	CrudeSquareCase crude_square;                                 ///< `crude_square`
	CrudeTriangleCase crude_triangle;                             ///< `crude_triangle`
	SyncedPulseCase synced_pulse;                                 ///< `synced_pulse`
	WavetableCase wavetable;                                      ///< `wavetable`
	MonoGainCase mono_gain;                                       ///< `mono_gain`
	MonoVecGainCase mono_vec_gain;                                ///< `mono_vec_gain`
	StereoGainCase stereo_gain;                                   ///< `stereo_gain`
	DuckEnvelopeCase duck_envelope;                               ///< `duck_envelope`
	ShiftTrackerCase shift_tracker;                               ///< `shift_tracker`
	OnePoleLowPassCase one_pole_lp;                               ///< `one_pole_lp`
	OnePoleAllPassCase one_pole_ap;                               ///< `one_pole_ap`
	AntialiasedTanhCase antialiased_tanh;                         ///< `antialiased_tanh`
	Tanh3Case tanh3;                                              ///< `tanh3`
	DryWetBlendCase dry_wet_blend;                                ///< `dry_wet_blend`
	OnePoleX2Case one_pole_x2;                                    ///< `one_pole_x2`
	OnePoleX4Case one_pole_x4;                                    ///< `one_pole_x4`
	ShiftTrackerX4Case shift_tracker_x4;                          ///< `shift_tracker_x4`
	ShiftTrackerX8Case shift_tracker_x8;                          ///< `shift_tracker_x8`
	StereoAntialiasedTanhCase stereo_antialiased_tanh;            ///< `stereo_antialiased_tanh`
	DelayFeedbackLegacyAnalogCase delay_feedback_legacy_analog;   ///< `delay_feedback_legacy_analog`
	DelayFeedbackPassesAnalogCase delay_feedback_passes_analog;   ///< `delay_feedback_passes_analog`
	DelayFeedbackChainAnalogCase delay_feedback_chain_analog;     ///< `delay_feedback_chain_analog`
	DelayFeedbackPassesDigitalCase delay_feedback_passes_digital; ///< `delay_feedback_passes_digital`
	DelayFeedbackChainDigitalCase delay_feedback_chain_digital;   ///< `delay_feedback_chain_digital`
	/// `delay_feedback_chain_digital_out_of_place`
	DelayFeedbackChainDigitalOutOfPlaceCase delay_feedback_chain_digital_out_of_place;
	Chain3PassesCase chain3_passes; ///< `chain3_passes`
	Chain3FusedCase chain3_fused;   ///< `chain3_fused`
	/// `delay_whole_native_straight_digital`
	DelayWholeCase delay_whole_native_straight_digital{delay_whole_mode(DelayTapeForm::NATIVE, false, false)};
	/// `delay_whole_native_straight_analog`
	DelayWholeCase delay_whole_native_straight_analog{delay_whole_mode(DelayTapeForm::NATIVE, false, true)};
	/// `delay_whole_native_cross_digital`
	DelayWholeCase delay_whole_native_cross_digital{delay_whole_mode(DelayTapeForm::NATIVE, true, false)};
	/// `delay_whole_native_cross_analog`
	DelayWholeCase delay_whole_native_cross_analog{delay_whole_mode(DelayTapeForm::NATIVE, true, true)};
	/// `delay_whole_res_lo_straight_digital`
	DelayWholeCase delay_whole_res_lo_straight_digital{delay_whole_mode(DelayTapeForm::RES_LO, false, false)};
	/// `delay_whole_res_lo_straight_analog`
	DelayWholeCase delay_whole_res_lo_straight_analog{delay_whole_mode(DelayTapeForm::RES_LO, false, true)};
	/// `delay_whole_res_lo_cross_digital`
	DelayWholeCase delay_whole_res_lo_cross_digital{delay_whole_mode(DelayTapeForm::RES_LO, true, false)};
	/// `delay_whole_res_lo_cross_analog`
	DelayWholeCase delay_whole_res_lo_cross_analog{delay_whole_mode(DelayTapeForm::RES_LO, true, true)};
	/// `delay_whole_res_hi_straight_digital`
	DelayWholeCase delay_whole_res_hi_straight_digital{delay_whole_mode(DelayTapeForm::RES_HI, false, false)};
	/// `delay_whole_res_hi_straight_analog`
	DelayWholeCase delay_whole_res_hi_straight_analog{delay_whole_mode(DelayTapeForm::RES_HI, false, true)};
	/// `delay_whole_res_hi_cross_digital`
	DelayWholeCase delay_whole_res_hi_cross_digital{delay_whole_mode(DelayTapeForm::RES_HI, true, false)};
	/// `delay_whole_res_hi_cross_analog`
	DelayWholeCase delay_whole_res_hi_cross_analog{delay_whole_mode(DelayTapeForm::RES_HI, true, true)};
	DelayWholeCase delay_whole_growing{kDelayWholeGrowing};       ///< `delay_whole_growing`
	FilterMonoCase filter_lp12_mono{kFilterLow12};                ///< `filter_lp12_mono`
	FilterStereoCase filter_lp12_stereo{kFilterLow12};            ///< `filter_lp12_stereo`
	FilterMonoCase filter_lp24_mono{kFilterLow24};                ///< `filter_lp24_mono`
	FilterStereoCase filter_lp24_stereo{kFilterLow24};            ///< `filter_lp24_stereo`
	FilterMonoCase filter_lpdrive_mono{kFilterLowDrive};          ///< `filter_lpdrive_mono`
	FilterStereoCase filter_lpdrive_stereo{kFilterLowDrive};      ///< `filter_lpdrive_stereo`
	FilterMonoCase filter_lpdrive_os_mono{kFilterLowDriveOs};     ///< `filter_lpdrive_os_mono`
	FilterStereoCase filter_lpdrive_os_stereo{kFilterLowDriveOs}; ///< `filter_lpdrive_os_stereo`
	FilterMonoCase filter_hp_mono{kFilterHigh};                   ///< `filter_hp_mono`
	FilterStereoCase filter_hp_stereo{kFilterHigh};               ///< `filter_hp_stereo`
	FilterMonoCase filter_svf_band_mono{kFilterSvfBand};          ///< `filter_svf_band_mono`
	FilterStereoCase filter_svf_band_stereo{kFilterSvfBand};      ///< `filter_svf_band_stereo`
	FilterMonoCase filter_svf_notch_mono{kFilterSvfNotch};        ///< `filter_svf_notch_mono`
	FilterStereoCase filter_svf_notch_stereo{kFilterSvfNotch};    ///< `filter_svf_notch_stereo`
	/// `filterset_hl_stereo`
	FilterStereoCase filterset_hl_stereo{filter_set_case(dsp::fixed::FilterRoute::HIGH_TO_LOW)};
	/// `filterset_lh_stereo`
	FilterStereoCase filterset_lh_stereo{filter_set_case(dsp::fixed::FilterRoute::LOW_TO_HIGH)};
	/// `filterset_par_stereo`
	FilterStereoCase filterset_par_stereo{filter_set_case(dsp::fixed::FilterRoute::PARALLEL)};
	FilterStereoCase filter_lp24_fading_stereo{kFilterLow24Fading};           ///< `filter_lp24_fading_stereo`
	CompressorCase compressor_wet_render{kCompressorWetRender};               ///< `compressor_wet_render`
	CompressorCase compressor_wet_neutral{kCompressorWetNeutral};             ///< `compressor_wet_neutral`
	CompressorCase compressor_blend_render{kCompressorBlendRender};           ///< `compressor_blend_render`
	CompressorCase compressor_blend_neutral{kCompressorBlendNeutral};         ///< `compressor_blend_neutral`
	ModFxCase mod_fx_chorus_stereo{kModFxChorusStereo};                       ///< `mod_fx_chorus_stereo`
	ModFxCase mod_fx_chorus_mono{kModFxChorusMono};                           ///< `mod_fx_chorus_mono`
	ModFxCase mod_fx_chorus_stereo_stereo{kModFxChorusStereoStereo};          ///< `mod_fx_chorus_stereo_stereo`
	ModFxCase mod_fx_chorus_stereo_mono{kModFxChorusStereoMono};              ///< `mod_fx_chorus_stereo_mono`
	ModFxCase mod_fx_dimension_stereo{kModFxDimensionStereo};                 ///< `mod_fx_dimension_stereo`
	ModFxCase mod_fx_dimension_mono{kModFxDimensionMono};                     ///< `mod_fx_dimension_mono`
	ModFxCase mod_fx_flanger_stereo{kModFxFlangerStereo};                     ///< `mod_fx_flanger_stereo`
	ModFxCase mod_fx_flanger_mono{kModFxFlangerMono};                         ///< `mod_fx_flanger_mono`
	ModFxCase mod_fx_warble_stereo{kModFxWarbleStereo};                       ///< `mod_fx_warble_stereo`
	ModFxCase mod_fx_warble_mono{kModFxWarbleMono};                           ///< `mod_fx_warble_mono`
	ModFxCase mod_fx_phaser{kModFxPhaser};                                    ///< `mod_fx_phaser`
	GranularCase granular_sparse_unison{kGranularSparseUnison};               ///< `granular_sparse_unison`
	GranularCase granular_sparse_random{kGranularSparseRandom};               ///< `granular_sparse_random`
	GranularCase granular_dense_unison{kGranularDenseUnison};                 ///< `granular_dense_unison`
	GranularCase granular_dense_random{kGranularDenseRandom};                 ///< `granular_dense_random`
	StutterCase stutter_record_native{{.play = false, .resampled = false}};   ///< `stutter_record_native`
	StutterCase stutter_record_resampled{{.play = false, .resampled = true}}; ///< `stutter_record_resampled`
	StutterCase stutter_play_native{{.play = true, .resampled = false}};      ///< `stutter_play_native`
	StutterCase stutter_play_resampled{{.play = true, .resampled = true}};    ///< `stutter_play_resampled`
};

/// @brief The cases' components, built on first use.
/// @return The one CaseSet every case in cases() acts on.
[[nodiscard]] CaseSet& case_set();

/// @brief Which of the eight case lists a run prints.
enum class CaseList : uint8_t {
	KERNELS, ///< One case per component: the calibration loop, then each kernel in its Mono, Stereo or Control.
	LANES,   ///< The calibration loop, then each `Lanes` case beside the mono case of its kernel.
	CHAIN,   ///< The calibration loop, then stages run as separate passes beside the same stages as one `Chain`.
	/// The calibration loop, the whole delay block in every tape form, mix and shaper, then the feedback chain's
	/// two cases from the chain list, unchanged code that bounds how much an image's code placement moves the rest.
	DELAY,
	/// The calibration loop, each filter mono and stereo, the filter set on each route, then the feedback chain's two
	/// cases from the chain list, as in the delay list.
	FILTERS,
	/// The calibration loop, the compressor fully wet and blended through `render` and `renderVolumeNeutral`, then the
	/// feedback chain's two cases from the chain list, as in the delay list.
	COMPRESSOR,
	/// The calibration loop, each mod FX type as its owners render it through `effects::ModFx`, in stereo and in mono
	/// (the phaser once), then the feedback chain's two cases from the chain list, as in the delay list.
	MOD_FX,
	/// The calibration loop, the granular effect as its owners render it through `effects::Granular`, with sparse and
	/// dense grains at no and full pitch randomness, then the feedback chain's two cases from the chain list, as in the
	/// delay list.
	GRANULAR,
	/// The calibration loop, the stutter as its owners render it, recording and playing over a native and a resampled
	/// tape, then the feedback chain's two cases from the chain list, as in the delay list.
	STUTTER,
};

/// @brief The case list a bench image runs: DELUGE_DSP_BENCH_CASES, as src/deluge/CMakeLists.txt defines it.
#if defined(DELUGE_DSP_BENCH_LANES)
inline constexpr CaseList kBenchCaseList = CaseList::LANES;
#elif defined(DELUGE_DSP_BENCH_CHAIN)
inline constexpr CaseList kBenchCaseList = CaseList::CHAIN;
#elif defined(DELUGE_DSP_BENCH_DELAY)
inline constexpr CaseList kBenchCaseList = CaseList::DELAY;
#elif defined(DELUGE_DSP_BENCH_FILTERS)
inline constexpr CaseList kBenchCaseList = CaseList::FILTERS;
#elif defined(DELUGE_DSP_BENCH_COMPRESSOR)
inline constexpr CaseList kBenchCaseList = CaseList::COMPRESSOR;
#elif defined(DELUGE_DSP_BENCH_MOD_FX)
inline constexpr CaseList kBenchCaseList = CaseList::MOD_FX;
#elif defined(DELUGE_DSP_BENCH_GRANULAR)
inline constexpr CaseList kBenchCaseList = CaseList::GRANULAR;
#elif defined(DELUGE_DSP_BENCH_STUTTER)
inline constexpr CaseList kBenchCaseList = CaseList::STUTTER;
#else
inline constexpr CaseList kBenchCaseList = CaseList::KERNELS;
#endif

/// @brief A case list, in the order each block size prints it.
/// @param list Which list.
/// @return Cases over members of case_set(), built on first use. Each list starts with `calibration_loop`, which the
/// host script's sanity check reads. A bench image builds only its own list (kBenchCaseList) and returns no cases for
/// any other, so the other lists' code stays out of on-chip SRAM.
[[nodiscard]] std::span<const Case> cases(CaseList list);

} // namespace deluge::bench
