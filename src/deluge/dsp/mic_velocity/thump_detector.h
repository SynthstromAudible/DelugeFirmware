/*
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

#pragma once

#include <array>
#include <cstdint>
#include <optional>

/// Finds the thump a pad makes in the internal mic, for mic velocity.
///
/// Measured on a Deluge (2026-11): a tap's thump is mostly 60-400 Hz, and the internal speaker hardly reaches the mic
/// below ~250 Hz. So the mic is decimated to 5.5 kHz, band-limited to 60-300 Hz, and the thump's level is its peak
/// from when it rises out of the noise until 4 ms later (which tracks the whole thump's peak with r 0.99). Soft,
/// normal and hard taps came out 9-14 dB apart, each within about 2 dB.
namespace deluge::dsp::mic_velocity {

constexpr int32_t kInputRate = 44100;
constexpr int32_t kDecimation = 8;
constexpr float kRate = static_cast<float>(kInputRate) / kDecimation; ///< 5512.5 Hz

/// Input frames (at 44.1 kHz) on the mic reader's clock
using Frame = uint32_t;

/// Turns 44.1 kHz q31 samples into 5.5 kHz floats (full scale 1). Two cascaded 8-sample moving sums: their nulls
/// sit on the multiples of 5.5 kHz, which are what would fold down into the thump's band.
class Decimator {
public:
	/// Returns a decimated sample every kDecimation inputs
	std::optional<float> push(int32_t sample);
	void reset();

private:
	std::array<int32_t, kDecimation> first_{};
	std::array<int64_t, kDecimation> second_{};
	int64_t sumFirst_ = 0;
	int64_t sumSecond_ = 0;
	int32_t index_ = 0;
};

/// A second-order section, direct form II transposed
struct Biquad {
	float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
	float z1 = 0, z2 = 0;

	float process(float x) {
		float const y = b0 * x + z1;
		z1 = b1 * x - a1 * y + z2;
		z2 = b2 * x - a2 * y;
		return y;
	}
	void reset() { z1 = z2 = 0; }

	static Biquad highPass1(float cutoff, float rate); ///< First order (b2 = a2 = 0)
	static Biquad lowPass2(float cutoff, float rate);  ///< Butterworth
	static Biquad lowPass1(float cutoff, float rate);  ///< First order
};

/// A thump: where it rose out of the noise, and how loud it got in the 4 ms after
struct Thump {
	Frame onset = 0;
	float level = 0;      ///< Peak of the band-limited mic, full scale 1
	bool settled = false; ///< Its 4 ms have passed, so level won't change
};

class ThumpDetector {
public:
	/// How long after onset the level is measured for
	static constexpr Frame kSettleFrames = kInputRate * 4 / 1000;
	/// A thump lasts at least this long
	static constexpr Frame kMinGapFrames = kInputRate * 15 / 1000;
	/// Rising this far over the noise floor starts a thump (12 dB)
	static constexpr float kOnsetOverFloor = 4.f;
	/// Never start a thump below this, however quiet the noise (-90 dBFS)
	static constexpr float kMinOnsetLevel = 3.2e-5f;
	static constexpr int32_t kNumThumps = 16;

	ThumpDetector();
	void reset();

	/// One decimated sample of the mic (after any echo cancelling); frame is the input frame it ends at
	void process(float sample, Frame frame);

	/// The noise floor: a typical 10 ms peak of the band-limited mic with no thump
	[[nodiscard]] float noiseFloor() const { return floor_; }
	/// The level a thump has to reach to be found
	[[nodiscard]] float onsetLevel() const;

	/// The most recent thumps, newest first (0: the newest). nullptr past the ones there are.
	[[nodiscard]] Thump const* recent(int32_t index) const;

	/// Whether a thump is in progress (started, not yet settled)
	[[nodiscard]] bool rising() const { return inThump_ && !thumps_[newest_].settled; }

private:
	Biquad highPass_;
	Biquad lowPass_;

	std::array<Thump, kNumThumps> thumps_{};
	int32_t newest_ = 0;
	int32_t count_ = 0;
	bool inThump_ = false;
	float thumpPeak_ = 0; // Over the whole thump, for telling when it has died away
	// After a thump, its peak decaying: a new thump has to beat it, so the pad's bounce and ringing (bursts 15-20 ms
	// apart, 10-15 dB down) don't start their own
	float holdOff_ = 0;

	// Noise floor, from 10 ms blocks that aren't in a thump
	float floor_ = 0;
	float blockMax_ = 0;
	int32_t blockSamples_ = 0;
	bool blockHadThump_ = false;
	bool floorStarted_ = false;
	int32_t sustainedBlocks_ = 0;
};

} // namespace deluge::dsp::mic_velocity
