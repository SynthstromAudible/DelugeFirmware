/*
 * Copyright © 2024 Mark Adams and Alter-Alter
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

/// @file
/// @brief Test-local reference for the granular effect: the app's `GranularProcessor` as it rendered before the
/// kernel replaced it, copied line for line. The kernel is held bit-exact to it.
///
/// @ref deluge::dsp::test::ReferenceGranular differs from it only where the test has no firmware around it:
///  - the ring is an owned array with a `held` flag, where the firmware holds a heap block; every acquisition of the
///    ring zeroes it, as the host allocator does, so a slot read before it is written since the acquisition reads 0;
///  - the noise draws (`CONG`, `random`, `getRandom255`, `sampleTriangleDistribution`) go through a Noise policy that
///    defaults to the global `jcong`;
///  - the reverb feed (`AudioEngine::feedReverbBackdoorForGrain`) adds into a span argument, wrapping;
///  - every add that can wrap is a wrapping one, so the spec's signed-overflow sanitizer sees the same two's-complement
///    result the target computes;
///  - `shouldDoPanning` and `quickLog` are the dsp-next functions they delegate to;
///  - the unused tempo argument and `grainLastTickCountIsZero` are gone;
///  - counters and read accessors the spec asserts through, which the output does not depend on.

#pragma once

#include "definitions_cxx.hpp"
#include "dsp-next/core/stereo_sample.h"
#include "dsp-next/core/wrapping.h"
#include "dsp-next/fixed/curves/quick_log.h"
#include "dsp-next/fixed/filter/one_pole.h"
#include "dsp-next/fixed/vca/pan.h"
#include "granular_ring.h"
#include "util/fixedpoint.h"
#include "util/waves.h" // CONG, jcong

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <span>

namespace deluge::dsp::test {

/// @brief The synth-wide noise generator: the stream `CONG` steps, which the app's granular draws from.
struct GlobalNoise {
	/// @return The next word of the global stream.
	[[nodiscard]] static uint32_t next() { return CONG; }
};

/// @brief The app's granular processor, copied line for line: what the kernel must match.
/// @tparam Noise Where the draws come from: a type with `static uint32_t next()`.
template <class Noise = GlobalNoise>
class BasicReferenceGranular {
public:
	/// @brief How many adds have wrapped since the last @ref reset_counters.
	struct Wraps {
		std::size_t wet_sum = 0; ///< `wetl + wetr`, before the reverb amount scales it.
		std::size_t send = 0;    ///< The add into the reverb feed.
	};
	static inline Wraps wraps{};
	/// @brief How many grains of each pitch class were drawn since the last @ref reset_counters: the cases
	/// -3 to 3 in order, then the default arm.
	static inline std::array<std::size_t, 8> pitch_classes{};
	/// @brief How many reads of a ring slot not written since the ring was last acquired.
	static inline std::size_t unwritten_reads = 0;

	static void reset_counters() {
		wraps = {};
		pitch_classes.fill(0);
		unwritten_reads = 0;
	}

	BasicReferenceGranular() {
		wrapsToShutdown = 0;
		bufferWriteIndex = 0;
		_grainShift = 13230; // 300ms
		_grainSize = 13230;  // 300ms
		_grainRate = 1260;   // 35hz
		_grainFeedbackVol = 161061273;
		for (auto& grain : grains) {
			grain.length = 0;
		}
		_grainVol = 0;
		_grainDryVol = 2147483647;
		_pitchRandomness = 0;
		grainInitialized = false;
	}

	[[nodiscard]] int32_t getSamplesToShutdown() const { return wrapsToShutdown * kModFXGrainBufferSize; }

	/// @return Whether a call has started with the write index at the ring's end, which sets the grains free of the
	/// first pass.
	[[nodiscard]] bool grain_initialized() const { return grainInitialized; }
	/// @return Whether the ring is held.
	[[nodiscard]] bool holding() const { return held; }
	/// @return How many times the ring was acquired: zeroed afresh.
	[[nodiscard]] std::size_t acquisitions() const { return acquisitions_; }

	/// @brief The values the knobs last set, and the knob positions they were set from.
	struct Cache {
		int32_t size;     ///< `_grainSize`, frames.
		int32_t period;   ///< `_grainRate`, frames.
		int32_t wet;      ///< `_grainVol`, Q31.
		int32_t dry;      ///< `_grainDryVol`, Q31.
		int32_t feedback; ///< `_grainFeedbackVol`, Q31.
		int32_t rate;     ///< `_rateKnobPos`.
		int32_t mix;      ///< `_mixKnobPos`.
		int32_t density;  ///< `_densityKnobPos`.
	};
	/// @return The values the knobs last set.
	[[nodiscard]] Cache cache() const {
		return {.size = _grainSize,
		        .period = _grainRate,
		        .wet = _grainVol,
		        .dry = _grainDryVol,
		        .feedback = _grainFeedbackVol,
		        .rate = _rateKnobPos,
		        .mix = _mixKnobPos,
		        .density = _densityKnobPos};
	}
	/// @return The index the next frame is written to, before its lazy wrap.
	[[nodiscard]] uint32_t write_index() const { return bufferWriteIndex; }
	/// @return Whether grains may start.
	[[nodiscard]] bool buffer_full() const { return bufferFull; }
	/// @return Ring laps left before the ring is released.
	[[nodiscard]] int32_t wraps_to_shutdown() const { return wrapsToShutdown; }
	/// @return The grains.
	[[nodiscard]] const auto& grain_states() const { return grains; }
	/// @return The wet low-pass memories, left then right.
	[[nodiscard]] std::array<deluge::dsp::fixed::OnePole::State, 2> low_pass() const {
		return {lpf_state_l_, lpf_state_r_};
	}
	/// @return The ring.
	[[nodiscard]] std::span<const StereoSample> ring_frames() const { return ring; }

	/// allows the buffer to be stolen
	void startSkippingRendering() {
		// The instrument has gone idle — release the buffer outright instead of leaving it allocated-but-stealable.
		releaseBuffer();
	}

	/// preset is currently converted from a param to a 0-4 preset inside the grain, which is probably not great
	void processGrainFX(std::span<StereoSample> buffer, int32_t grainRate, int32_t grainMix, int32_t grainDensity,
	                    int32_t pitchRandomness, int32_t* postFXVolume, bool anySoundComingIn, q31_t reverbAmount,
	                    std::span<q31_t> send) {
		if (anySoundComingIn || wrapsToShutdown >= 0) {
			if (!held) {
				getBuffer();
			}
			if (anySoundComingIn) {
				setWrapsToShutdown();
			}
			setupGrainFX(grainRate, grainMix, grainDensity, pitchRandomness, postFXVolume);
			int i = 0;
			for (StereoSample& sample : buffer) {
				StereoSample grainWet = processOneGrainSample(sample);
				auto wetl = q31_mult(grainWet.l, _grainVol);
				auto wetr = q31_mult(grainWet.r, _grainVol);

				wetl = OnePole::step<OnePole::Mode::LOW_PASS>(kWetLowPass, {}, lpf_state_l_, wetl);
				wetr = OnePole::step<OnePole::Mode::LOW_PASS>(kWetLowPass, {}, lpf_state_r_, wetr);

				// WET and DRY Vol
				sample.l = add_saturate(q31_mult(sample.l, _grainDryVol), wetl);
				sample.r = add_saturate(q31_mult(sample.r, _grainDryVol), wetr);

				// adding a small amount of extra reverb covers a lot of the granular artifacts
				feedReverbBackdoorForGrain(send, i, q31_mult(add(wraps.wet_sum, wetl, wetr), reverbAmount));
				i += 1;
			}

			if (wrapsToShutdown < 0) {
				// Fully decayed and silent: release the ~512 KB buffer outright rather than merely marking it
				// stealable, so an idle granular FX stops holding SDRAM until something else forces a reclaim.
				releaseBuffer();
			}
		}
		if (bufferWriteIndex > kModFXGrainBufferSize / 2) {
			bufferFull = true; // we now know we have enough written to start generating grains
		}
	}

	void clearGrainFXBuffer() {
		for (int i = 0; i < 8; i++) {
			grains[i].length = 0;
		}
		grainInitialized = false;
		bufferWriteIndex = 0;
		getBuffer();
	}

private:
	using OnePole = deluge::dsp::fixed::OnePole;

	struct Grain {
		int32_t length;     // in samples 0=OFF
		int32_t startPoint; // starttimepos in samples
		int32_t counter;    // relative pos in samples
		uint16_t pitch;     // 1024=1.0
		int32_t volScale;
		int32_t volScaleMax;
		bool rev;        // 0=normal, 1 =reverse
		int32_t panVolL; // 0 - 1073741823
		int32_t panVolR; // 0 - 1073741823
	};

	[[nodiscard]] static uint8_t getRandom255() { return Noise::next() >> 24; }

	[[nodiscard]] static int32_t getNoise() { return static_cast<int32_t>(Noise::next()); }

	/// generates a triangle distribution from -1q31 to 1q31 centered on 0 (Irwin-Hall)
	[[nodiscard]] static q31_t sampleTriangleDistribution() {
		auto u1 = getNoise();
		auto u2 = getNoise();
		auto s = add_saturate(u1, u2);
		return s;
	}

	[[nodiscard]] static int32_t random(int32_t upperLimit) {
		return static_cast<uint16_t>(Noise::next() >> 16) % (upperLimit + 1);
	}

	/// @return @p a + @p b, wrapping, counted in @p counter when it wraps.
	[[nodiscard]] static q31_t add(std::size_t& counter, q31_t a, q31_t b) {
		counter += static_cast<std::size_t>(static_cast<int64_t>(a) + b != static_cast<int64_t>(wrapping_add(a, b)));
		return wrapping_add(a, b);
	}

	static void feedReverbBackdoorForGrain(std::span<q31_t> send, int index, q31_t value) {
		const auto i = static_cast<std::size_t>(index);
		send[i] = add(wraps.send, send[i], value);
	}

	void setWrapsToShutdown() {

		if (_grainFeedbackVol < 33554432) {
			wrapsToShutdown = 1;
		}
		else if (_grainFeedbackVol <= 100663296) {
			wrapsToShutdown = 2;
		}
		else if (_grainFeedbackVol <= 218103808) {
			wrapsToShutdown = 3;
		}
		// max possible, feedback doesn't go very high
		else {
			wrapsToShutdown = 4;
		}
	}

	void setupGrainFX(int32_t grainRate, int32_t grainMix, int32_t grainDensity, int32_t pitchRandomness,
	                  int32_t* postFXVolume) {
		if (!grainInitialized && bufferWriteIndex >= 65536) {
			grainInitialized = true;
		}
		*postFXVolume = multiply_32x32_rshift32(*postFXVolume, ONE_OVER_SQRT2_Q31) << 1; // Divide by sqrt(2)
		                                                                                 // Shift
		_grainShift = 44 * 300; // this is where we should tempo sync ( it's kSampleRate / 1000 * 300 for a 300ms base
		                        // delay amount);
		// grain size depends on both density and rate, check if either has changed
		const bool grain_size_changed = _densityKnobPos != grainDensity || _rateKnobPos != grainRate;
		// Rate
		if (_rateKnobPos != grainRate) {
			_rateKnobPos = grainRate;
			int32_t grainRateRaw =
			    std::clamp<int32_t>((deluge::dsp::fixed::quick_log(grainRate) - 364249088) >> 21, 0, 256);

			_grainRate = ((360 * grainRateRaw >> 8) * grainRateRaw >> 8); // 0 - 180hz
			_grainRate = std::max<int32_t>(1, _grainRate);
			_grainRate = (kSampleRate << 1) / _grainRate;
		}
		// if grain size changed, calculate density and re-calculate grain size
		if (grain_size_changed) {
			_densityKnobPos = grainDensity;
			q31_t density = ((grainDensity / 2) + (1073741824)); // convert to 0-2^31
			// the maximum length is 8x the rate, past that grains get stolen for new grains. This keeps a consistent
			// proportion of grain sound as you increase the rate
			_grainSize = 1760 + q31_mult(_grainRate << 3, density);
		}
		// this is only 2 cycles so there's no point in checking
		_pitchRandomness = toPositive(pitchRandomness);
		// Volume
		if (_mixKnobPos != grainMix) {
			_mixKnobPos = grainMix;
			_grainVol = grainMix - 2147483648;
			_grainVol =
			    (multiply_32x32_rshift32_rounded(multiply_32x32_rshift32_rounded(_grainVol, _grainVol), _grainVol) << 2)
			    + 2147483648; // Cubic
			_grainVol = std::max<int32_t>(0, std::min<int32_t>(2147483647, _grainVol));
			_grainDryVol =
			    static_cast<int32_t>(std::clamp<int64_t>(((int64_t{2147483648} - _grainVol) << 3), 0, 2147483647));
			_grainFeedbackVol = _grainVol >> 1;
		}
	}

	StereoSample processOneGrainSample(StereoSample currentSample) {
		if (bufferWriteIndex >= kModFXGrainBufferSize) {
			bufferWriteIndex = 0;
			wrapsToShutdown -= 1;
		}
		int32_t writeIndex = bufferWriteIndex; // % kModFXGrainBufferSize
		if (bufferFull && bufferWriteIndex % _grainRate == 0) [[unlikely]] {
			setupGrainsIfNeeded(writeIndex);
		}

		int32_t grains_l = 0;
		int32_t grains_r = 0;
		for (int32_t i = 0; i < 8; i++) {
			if (grains[i].length > 0) {
				// triangle window
				int32_t vol =
				    grains[i].counter <= (grains[i].length >> 1)
				        ? grains[i].counter * grains[i].volScale
				        : grains[i].volScaleMax - (grains[i].counter - (grains[i].length >> 1)) * grains[i].volScale;
				int32_t delta = grains[i].counter * (grains[i].rev == 1 ? -1 : 1);
				if (grains[i].pitch != 1024) {
					delta = ((delta * grains[i].pitch) >> 10);
				}
				int32_t pos = (grains[i].startPoint + delta + kModFXGrainBufferSize) & kModFXGrainBufferIndexMask;
				unwritten_reads += static_cast<std::size_t>(!written_.test(static_cast<std::size_t>(pos)));
				grains_l = multiply_accumulate_32x32_rshift32_rounded(
				    grains_l, multiply_32x32_rshift32(ring[pos].l, vol) << 0, grains[i].panVolL);
				grains_r = multiply_accumulate_32x32_rshift32_rounded(
				    grains_r, multiply_32x32_rshift32(ring[pos].r, vol) << 0, grains[i].panVolR);

				grains[i].counter++;
				if (grains[i].counter >= grains[i].length) {
					grains[i].length = 0;
				}
			}
		}

		grains_l <<= 3;
		grains_r <<= 3;
		// Feedback (Below grainFeedbackVol means "grainVol >> 4")
		ring[writeIndex].l = multiply_accumulate_32x32_rshift32_rounded(currentSample.l, grains_l, _grainFeedbackVol);
		ring[writeIndex].r = multiply_accumulate_32x32_rshift32_rounded(currentSample.r, grains_r, _grainFeedbackVol);
		written_.set(static_cast<std::size_t>(writeIndex));

		bufferWriteIndex++;
		return StereoSample{grains_l, grains_r};
	}

	void setupGrainsIfNeeded(int32_t writeIndex) {
		for (int32_t i = 0; i < 8; i++) {
			if (grains[i].length <= 0) {
				grains[i].length = _grainSize;
				int32_t spray = random(kModFXGrainBufferSize >> 1) - (kModFXGrainBufferSize >> 2);
				grains[i].startPoint =
				    (bufferWriteIndex + kModFXGrainBufferSize - _grainShift + spray) & kModFXGrainBufferIndexMask;
				grains[i].counter = 0;
				grains[i].rev = (getRandom255() < 76);

				// randomly select a type of grain to generate, options are based on the amount of randomness
				int8_t typeRand = multiply_32x32_rshift32(q31_mult(sampleTriangleDistribution(), _pitchRandomness), 7);
				pitch_classes[typeRand >= -3 && typeRand <= 3 ? static_cast<std::size_t>(typeRand + 3) : 7] += 1;
				switch (typeRand) {

				case -3:
					grains[i].pitch = 512; // octave down
					grains[i].rev = true;
					break;
				case -2:
					grains[i].pitch = 767; // 4th down (e.g. it's the 5th)
					grains[i].rev = true;
					break;
				case -1:
					grains[i].pitch = 1024; // unison reverse
					grains[i].rev = true;
					break;
				case 0:
					grains[i].pitch = 1024; // unison
					break;
				case 1:
					grains[i].pitch = 2048; //  octave
					break;
				case 2:
					grains[i].pitch = 1534; // 5th
					break;
				case 3:
					grains[i].pitch = 2048; //  octave reverse
					grains[i].rev = true;
					break;
					// This is pretty rare even at max randomness
				default:
					grains[i].pitch = 3072; //  octave + 5th
					grains[i].rev = true;
					break;
				}
				if (grains[i].rev) {
					grains[i].startPoint = (writeIndex + kModFXGrainBufferSize - 1) & kModFXGrainBufferIndexMask;
					grains[i].length = (grains[i].pitch > 1024)
					                       ? std::min<int32_t>(grains[i].length, 21659)  // Buffer length*0.3305
					                       : std::min<int32_t>(grains[i].length, 30251); // 1.48s - 0.8s
				}
				else {
					if (grains[i].pitch > 1024) {
						int32_t startPointMax = (writeIndex + grains[i].length
						                         - ((grains[i].length * grains[i].pitch) >> 10) + kModFXGrainBufferSize)
						                        & kModFXGrainBufferIndexMask;
						if (!(grains[i].startPoint < startPointMax && grains[i].startPoint > writeIndex)) {
							grains[i].startPoint =
							    (startPointMax + kModFXGrainBufferSize - 1) & kModFXGrainBufferIndexMask;
						}
					}
					else if (grains[i].pitch < 1024) {
						int32_t startPointMax = (writeIndex + grains[i].length
						                         - ((grains[i].length * grains[i].pitch) >> 10) + kModFXGrainBufferSize)
						                        & kModFXGrainBufferIndexMask;

						if (!(grains[i].startPoint > startPointMax && grains[i].startPoint < writeIndex)) {
							grains[i].startPoint =
							    (writeIndex + kModFXGrainBufferSize - 1) & kModFXGrainBufferIndexMask;
						}
					}
				}
				if (!grainInitialized) {
					if (!grains[i].rev) { // forward
						grains[i].pitch = 1024;
						if (bufferWriteIndex > 13231) {
							int32_t newStartPoint = std::max<int32_t>(440, random(bufferWriteIndex - 2));
							grains[i].startPoint =
							    (writeIndex - newStartPoint + kModFXGrainBufferSize) & kModFXGrainBufferIndexMask;
						}
						else {
							grains[i].length = 0;
						}
					}
					else {
						grains[i].pitch = std::min<int32_t>(grains[i].pitch, 1024);
						if (bufferWriteIndex > 13231) {
							grains[i].length = std::min<int32_t>(grains[i].length, bufferWriteIndex - 2);
							grains[i].startPoint =
							    (writeIndex - 1 + kModFXGrainBufferSize) & kModFXGrainBufferIndexMask;
						}
						else {
							grains[i].length = 0;
						}
					}
				}
				if (grains[i].length > 0) {
					grains[i].volScale = (2147483647 / (grains[i].length >> 1));
					grains[i].volScaleMax = grains[i].volScale * (grains[i].length >> 1);
					const deluge::dsp::fixed::PanAmplitudes pan =
					    deluge::dsp::fixed::pan_amplitudes((getRandom255() - 128) << 23); // Pan Law 0
					grains[i].panVolL = pan.left;
					grains[i].panVolR = pan.right;
				}
				break;
			}
		}
	}

	void releaseBuffer() {
		held = false;
		bufferFull = false;
		bufferWriteIndex = 0;
	}

	void getBuffer() {
		if (!held) {
			// Every allocation reads zero on the host: the ring is zeroed whenever it is acquired afresh.
			ring.fill(StereoSample{0, 0});
			written_.reset();
			held = true;
			acquisitions_ += 1;
		}
		bufferFull =
		    false; // "clear" the buffer by stopping grains from being generated until it's refilled with fresh data
		bufferWriteIndex = 0;
	}

	// parameters
	uint32_t bufferWriteIndex;
	int32_t _grainSize;
	int32_t _grainRate;
	int32_t _grainShift;
	int32_t _grainFeedbackVol;
	int32_t _grainVol;
	int32_t _grainDryVol;
	int32_t _pitchRandomness;

	bool grainInitialized;

	Grain grains[8]{};

	int32_t wrapsToShutdown;
	std::array<StereoSample, kModFXGrainBufferSize> ring{};
	bool held{false};
	std::bitset<kModFXGrainBufferSize> written_{};
	std::size_t acquisitions_ = 0;
	int32_t _densityKnobPos{0};
	int32_t _rateKnobPos{0};
	int32_t _mixKnobPos{0};
	/// @brief The wet signal's low-pass, one pole at about 4.5 kHz: a moveability of 1/4, Q31 (tan(f) = 1/3).
	static constexpr OnePole::Coefficients kWetLowPass = OnePole::coefficients({.moveability = 1 << 29});
	/// @brief The wet low-pass memories, kept across blocks and grain changes.
	OnePole::State lpf_state_l_{};
	OnePole::State lpf_state_r_{}; ///< @copydoc lpf_state_l_
	bool bufferFull{false};
};

/// @brief The reference over the global noise stream.
using ReferenceGranular = BasicReferenceGranular<>;

} // namespace deluge::dsp::test
