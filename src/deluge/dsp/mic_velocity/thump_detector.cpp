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

#include "dsp/mic_velocity/thump_detector.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace deluge::dsp::mic_velocity {

namespace {

// The band the thump is measured in: under 60 Hz is handling rumble, over 300 Hz is mostly the speaker
constexpr float kHighPassHz = 60.f;
constexpr float kLowPassHz = 300.f;

// Noise floor: 10 ms blocks; it falls quickly and rises slowly (~200 ms), so thumps barely move it
constexpr int32_t kBlockSamples = static_cast<int32_t>(kRate / 100);
constexpr float kFloorFall = 0.5f;
constexpr float kFloorRise = 0.05f;
// Loudness that keeps going this many blocks is noise (a song starting), not a thump, so the floor follows it
constexpr int32_t kSustainedBlocks = 10;

// A thump ends once it has settled, kMinGapFrames have passed and the band is back under a quarter of its peak.
// It's ended regardless after this long.
constexpr float kEndFraction = 0.25f;
constexpr Frame kMaxThumpFrames = kInputRate * 60 / 1000;

// A thump that has settled is replaced by anything this much louder (6 dB): a small sound just before a pad's thump
// (a finger brushing it) mustn't take the thump's place
constexpr float kSupersede = 2.f;

// The hold-off after a thump decays with this time constant
constexpr float kHoldOffSeconds = 0.015f;

constexpr float kQ31 = 2147483648.f;

} // namespace

std::optional<float> Decimator::push(int32_t sample) {
	sumFirst_ += sample - first_[index_];
	first_[index_] = sample;
	sumSecond_ += sumFirst_ - second_[index_];
	second_[index_] = sumFirst_;
	index_ = (index_ + 1) % kDecimation;
	if (index_ != 0) {
		return std::nullopt;
	}
	return static_cast<float>(sumSecond_) * (1.f / (kDecimation * kDecimation * kQ31));
}

void Decimator::reset() {
	first_.fill(0);
	second_.fill(0);
	sumFirst_ = sumSecond_ = 0;
	index_ = 0;
}

Biquad Biquad::highPass1(float cutoff, float rate) {
	float const k = std::tan(std::numbers::pi_v<float> * cutoff / rate);
	Biquad f;
	f.b0 = 1.f / (1.f + k);
	f.b1 = -f.b0;
	f.b2 = 0;
	f.a1 = (k - 1.f) / (k + 1.f);
	f.a2 = 0;
	return f;
}

Biquad Biquad::lowPass1(float cutoff, float rate) {
	float const k = std::tan(std::numbers::pi_v<float> * cutoff / rate);
	Biquad f;
	f.b0 = k / (1.f + k);
	f.b1 = f.b0;
	f.b2 = 0;
	f.a1 = (k - 1.f) / (k + 1.f);
	f.a2 = 0;
	return f;
}

Biquad Biquad::lowPass2(float cutoff, float rate) {
	float const k = std::tan(std::numbers::pi_v<float> * cutoff / rate);
	float const norm = 1.f / (1.f + std::numbers::sqrt2_v<float> * k + k * k);
	Biquad f;
	f.b0 = k * k * norm;
	f.b1 = 2.f * f.b0;
	f.b2 = f.b0;
	f.a1 = 2.f * (k * k - 1.f) * norm;
	f.a2 = (1.f - std::numbers::sqrt2_v<float> * k + k * k) * norm;
	return f;
}

ThumpDetector::ThumpDetector()
    : highPass_(Biquad::highPass1(kHighPassHz, kRate)), lowPass_(Biquad::lowPass2(kLowPassHz, kRate)) {
}

void ThumpDetector::reset() {
	highPass_.reset();
	lowPass_.reset();
	thumps_ = {};
	newest_ = 0;
	count_ = 0;
	inThump_ = false;
	thumpPeak_ = 0;
	holdOff_ = 0;
	floor_ = 0;
	blockMax_ = 0;
	blockSamples_ = 0;
	blockHadThump_ = false;
	floorStarted_ = false;
	sustainedBlocks_ = 0;
}

float ThumpDetector::onsetLevel() const {
	return std::max(floor_ * kOnsetOverFloor, kMinOnsetLevel);
}

Thump const* ThumpDetector::recent(int32_t index) const {
	if (index < 0 || index >= std::min(count_, kNumThumps)) {
		return nullptr;
	}
	return &thumps_[(newest_ - index + kNumThumps) % kNumThumps];
}

void ThumpDetector::process(float sample, Frame frame) {
	float const y = std::abs(lowPass_.process(highPass_.process(sample)));

	static float const holdOffDecay = std::exp(-1.f / (kHoldOffSeconds * kRate));
	holdOff_ *= holdOffDecay;

	if (inThump_ && thumps_[newest_].settled && y > thumps_[newest_].level * kSupersede) {
		inThump_ = false; // Start a new one, below
		holdOff_ = 0;
	}

	if (inThump_) {
		Thump& thump = thumps_[newest_];
		thumpPeak_ = std::max(thumpPeak_, y);
		holdOff_ = std::max(holdOff_, thumpPeak_);
		if (!thump.settled) {
			thump.level = std::max(thump.level, y);
			thump.settled = frame - thump.onset >= kSettleFrames;
		}
		Frame const age = frame - thump.onset;
		if ((thump.settled && age >= kMinGapFrames && y < thumpPeak_ * kEndFraction) || age >= kMaxThumpFrames) {
			inThump_ = false;
		}
	}
	else if (floorStarted_ && y > std::max(onsetLevel(), holdOff_)) { // Nothing until there's a floor to beat
		newest_ = (newest_ + 1) % kNumThumps;
		thumps_[newest_] = Thump{.onset = frame, .level = y, .settled = false};
		count_ = std::min(count_ + 1, kNumThumps);
		inThump_ = true;
		thumpPeak_ = y;
	}

	// The noise floor
	blockMax_ = std::max(blockMax_, y);
	blockHadThump_ |= inThump_;
	if (++blockSamples_ >= kBlockSamples) {
		sustainedBlocks_ = blockHadThump_ ? sustainedBlocks_ + 1 : 0;
		if (!floorStarted_) {
			floor_ = blockMax_;
			floorStarted_ = true;
		}
		else if (!blockHadThump_ || sustainedBlocks_ > kSustainedBlocks) {
			floor_ += (blockMax_ - floor_) * (blockMax_ < floor_ ? kFloorFall : kFloorRise);
		}
		blockMax_ = 0;
		blockSamples_ = 0;
		blockHadThump_ = false;
	}
}

} // namespace deluge::dsp::mic_velocity
