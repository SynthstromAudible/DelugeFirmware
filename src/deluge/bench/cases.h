#pragma once

/// @file
/// @brief The DSP benchmark's case list: one case per dsp-next component, each configured as the voice configures it.
///
/// **Buffers.** Every case reads and writes the same statically allocated SRAM blocks: two q31 input blocks and one
/// q31 output block of kMaxBlock samples, and one stereo block of kMaxBlock frames. Each case's `setup` fills the
/// inputs it reads from a fixed-seed SeededNoise, so every run times the same data, and a case measures the SRAM
/// working set the voice renders in.
///
/// **State at timing.** The runner warms each case up before timing it, so what is timed is the case's steady
/// state. Each case's documentation says what that state is; the host spec checks each through its component's
/// public accessors.
///
/// **Rates.** Phase increments are UQ0.32 cycles per sample at 44.1 kHz: `frequency · 2^32 / 44100`.

#include "bench/runner.h"
#include "dsp-next/core/control.h"
#include "dsp-next/core/kernel.h"
#include "dsp-next/core/mono.h"
#include "dsp-next/core/phasor.h"
#include "dsp-next/core/ramp.h"
#include "dsp-next/core/stereo.h"
#include "dsp-next/core/types.h"
#include "dsp-next/fixed/dynamics/duck_envelope.h"
#include "dsp-next/fixed/osc/band_limited_table_oscillator.h"
#include "dsp-next/fixed/osc/crude_oscillators.h"
#include "dsp-next/fixed/osc/pulse_oscillator.h"
#include "dsp-next/fixed/vca/amplitude.h"
#include "dsp-next/fixed/vca/crude_amplitude.h"
#include "dsp-next/fixed/wavetable/wave_table_data.h"
#include "dsp-next/fixed/wavetable/wavetable_oscillator.h"
#include "util/fixedpoint.h"

#include <argon.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace deluge::bench {

using dsp::q31;
using dsp::StereoSample;

/// @brief The largest block a case renders, in samples (or frames): the buffers' size.
inline constexpr std::size_t kMaxBlock = 128;

/// @brief Blocks rendered from a case's setup to the end of its timed blocks, at the runner's defaults: warm-up
/// plus K. A case whose state drifts across a run stays representative for this many blocks of kMaxBlock.
inline constexpr std::size_t kBlocksPerRun = RunConfig{}.warmup_blocks + RunConfig{}.timed_blocks;

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
/// Amplitude is built from it at setup and runs every block from there, as one long block. Over a whole run
/// (kBlocksPerRun blocks of kMaxBlock) the ramp rises by under 9 · 10^6, so it stays mid-level and never saturates:
/// every block is the same arithmetic on the same inputs.
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
                  + (int64_t{AmplitudeCase::kRamp.increment()} * int64_t{kBlocksPerRun * kMaxBlock})
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
                  + (int64_t{CrudeAmplitudeCase::kRamp.increment()} * int64_t{kBlocksPerRun * kMaxBlock})
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

/// @brief `Control<DuckEnvelope>`: the sidechain's duck, configured then advanced once per block, as its owner does.
/// @details The rates are as the owner resolves them: the spec's attack of 37,640 per sample (222 samples), and a
/// release of 472 per sample (the release table's entry 40, 59 << 3: 17,772 samples), at the middle shape (fully
/// curved at the release's start). Setup places a hit of 0x60000000 4,665 samples ago: 222 samples of attack, then
/// 4,443 into the release, at position 4,443 · 472 = 2,097,096, a quarter of the stage.
///
/// **Timed state:** mid-release. Over a whole run the position rises by at most 8,704 · 472 = 4,108,288, to
/// 6,205,384, short of the stage's end at 2^23 = 8,388,608, and its curvedness, 65,536 - position / 128, stays above
/// zero: every block is the same curved-release arithmetic. Its cycles per sample, like every case's, divide the
/// block's cost by n, though the duck does its work once per block.
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

/// @brief Every case, by name.
struct CaseSet {
	CalibrationLoop calibration_loop;        ///< `calibration_loop`
	AmplitudeCase amplitude;                 ///< `amplitude`
	CrudeAmplitudeCase crude_amplitude;      ///< `crude_amplitude`
	PulseCase pulse;                         ///< `pulse`
	BandLimitedTableCase band_limited_table; ///< `band_limited_table`
	CrudeSawCase crude_saw;                  ///< `crude_saw`
	CrudeSquareCase crude_square;            ///< `crude_square`
	CrudeTriangleCase crude_triangle;        ///< `crude_triangle`
	SyncedPulseCase synced_pulse;            ///< `synced_pulse`
	WavetableCase wavetable;                 ///< `wavetable`
	MonoGainCase mono_gain;                  ///< `mono_gain`
	MonoVecGainCase mono_vec_gain;           ///< `mono_vec_gain`
	StereoGainCase stereo_gain;              ///< `stereo_gain`
	DuckEnvelopeCase duck_envelope;          ///< `duck_envelope`
};

/// @brief The cases' components, built on first use.
/// @return The one CaseSet every case in cases() acts on.
[[nodiscard]] CaseSet& case_set();

/// @brief The case list, in the order each block size prints it.
/// @return One case per member of case_set(), built on first use.
[[nodiscard]] std::span<const Case> cases();

} // namespace deluge::bench
