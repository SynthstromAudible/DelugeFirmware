/*
 * Copyright © 2024 Mark Adams and Alter-Alter
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

#include "GranularProcessor.h"
#include "OSLikeStuff/scheduler_api.h"
#include "definitions_cxx.hpp"
#include "dsp/filter/ladder_components.h"
#include "dsp_ng/core/types.hpp"
#include "memory/stealable.h"
#include "modulation/lfo.h"
#include <span>

class UnpatchedParamSet;

namespace deluge::dsp {

struct Grain {
	int32_t length = 0; // in samples 0=OFF
	int32_t startPoint; // starttimepos in samples
	int32_t counter;    // relative pos in samples
	uint16_t pitch;     // 1024=1.0
	int32_t volScale;
	int32_t volScaleMax;
	bool rev;        // 0=normal, 1 =reverse
	int32_t panVolL; // 0 - 1073741823
	int32_t panVolR; // 0 - 1073741823
};
class GrainBuffer;

/// The granular processor is the config and the grain states. It will seperately manage a 4mb stealable buffer for its
/// memory
class GranularProcessor {
public:
	GranularProcessor() { getBuffer(); }
	GranularProcessor(const GranularProcessor& other); // copy constructor
	~GranularProcessor();
	[[nodiscard]] int32_t getSamplesToShutdown() const { return wrapsToShutdown * kModFXGrainBufferSize; }

	/// allows the buffer to be stolen
	void startSkippingRendering();

	/// preset is currently converted from a param to a 0-4 preset inside the grain, which is probably not great
	void processGrainFX(StereoBuffer<q31_t> buffer, int32_t grainRate, int32_t grainMix, int32_t grainDensity,
	                    int32_t pitchRandomness, int32_t* postFXVolume, bool anySoundComingIn, float tempoBPM,
	                    q31_t reverbAmount);

	void clearGrainFXBuffer();
	void grainBufferStolen() { grainBuffer = nullptr; }

private:
	void setupGrainFX(int32_t grainRate, int32_t grainMix, int32_t grainDensity, int32_t pitchRandomness,
	                  int32_t* postFXVolume, float timePerInternalTick);
	StereoSample<q31_t> processOneGrainSample(StereoSample<q31_t> currentSample);
	void getBuffer();
	void setWrapsToShutdown();
	void setupGrainsIfNeeded(int32_t writeIndex);
	// parameters
	uint32_t bufferWriteIndex = 0;
	int32_t _grainSize = 13230;                                 // 300ms
	int32_t _grainRate = 1260;                                  // 35hz
	int32_t _grainShift = 13230;                                // 300ms
	int32_t _grainFeedbackVol = 161061273;                      // Q30: 0.15
	int32_t _grainVol = 0;                                      // Q31: 0
	int32_t _grainDryVol = std::numeric_limits<int32_t>::max(); // Q31: 1
	int32_t _pitchRandomness = 0;

	bool grainLastTickCountIsZero = true;
	bool grainInitialized = false;

	std::array<Grain, 8> grains = {0};

	int32_t wrapsToShutdown = 0;
	GrainBuffer* grainBuffer = nullptr;
	int32_t _densityKnobPos = 0;
	int32_t _rateKnobPos = 0;
	int32_t _mixKnobPos = 0; // Q31
	deluge::dsp::filter::BasicFilterComponent lpf_l{};
	deluge::dsp::filter::BasicFilterComponent lpf_r{};
	bool tempoSync = true;
	bool bufferFull = false;
};

class GrainBuffer : public Stealable {
public:
	GrainBuffer() = delete;
	GrainBuffer(GrainBuffer& other) = delete;
	GrainBuffer(const GrainBuffer& other) = delete;
	explicit GrainBuffer(GranularProcessor* grainFX) : owner(grainFX) {}
	bool mayBeStolen(void* thingNotToStealFrom) override {
		if (thingNotToStealFrom != this) {
			return !inUse;
		}
		return false;
	};
	void steal(char const* errorCode) override { owner->grainBufferStolen(); };

	// gives it  a high priority - these are huge so reallocating them can be slow
	[[nodiscard]] StealableQueue getAppropriateQueue() const override {
		return StealableQueue::CURRENT_SONG_SAMPLE_DATA_REPITCHED_CACHE;
	};
	StereoSample<q31_t>& operator[](int32_t i) { return sampleBuffer[i]; }
	StereoSample<q31_t> operator[](int32_t i) const { return sampleBuffer[i]; }
	bool inUse = true;

private:
	GranularProcessor* owner;
	std::array<StereoSample<q31_t>, kModFXGrainBufferSize * sizeof(StereoSample<q31_t>)> sampleBuffer;
};
} // namespace deluge::dsp
