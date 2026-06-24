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
#include "dsp/stereo_sample.h"
#include "modulation/lfo.h"
#include <span>

class UnpatchedParamSet;

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
class GrainBuffer;

/// The granular processor is the config and the grain states. It will seperately manage a 4mb stealable buffer for its
/// memory
class GranularProcessor {
public:
	GranularProcessor();
	GranularProcessor(const GranularProcessor& other); // copy constructor
	~GranularProcessor();
	[[nodiscard]] int32_t getSamplesToShutdown() const { return wrapsToShutdown * kModFXGrainBufferSize; }

	/// allows the buffer to be stolen
	void startSkippingRendering();

	/// preset is currently converted from a param to a 0-4 preset inside the grain, which is probably not great
	void processGrainFX(std::span<StereoSample> buffer, int32_t grainRate, int32_t grainMix, int32_t grainDensity,
	                    int32_t pitchRandomness, int32_t* postFXVolume, bool anySoundComingIn, float tempoBPM,
	                    q31_t reverbAmount);

	void clearGrainFXBuffer();

private:
	void setupGrainFX(int32_t grainRate, int32_t grainMix, int32_t grainDensity, int32_t pitchRandomness,
	                  int32_t* postFXVolume, float timePerInternalTick);
	StereoSample processOneGrainSample(StereoSample currentSample);
	void getBuffer();
	/// Free the grain buffer (if any), null the pointer, and reset write state. The grain buffer is a
	/// plain owner-managed heap block (not resource-manager-owned), released outright here on idle.
	void releaseBuffer();
	void setWrapsToShutdown();
	void setupGrainsIfNeeded(int32_t writeIndex);
	// parameters
	uint32_t bufferWriteIndex;
	int32_t _grainSize;
	int32_t _grainRate;
	int32_t _grainShift;
	int32_t _grainFeedbackVol;
	int32_t _grainVol;
	int32_t _grainDryVol;
	int32_t _pitchRandomness;

	bool grainLastTickCountIsZero;
	bool grainInitialized;

	Grain grains[8]{};

	int32_t wrapsToShutdown;
	GrainBuffer* grainBuffer{};
	int32_t _densityKnobPos{0};
	int32_t _rateKnobPos{0};
	int32_t _mixKnobPos{0};
	deluge::dsp::filter::BasicFilterComponent lpf_l{};
	deluge::dsp::filter::BasicFilterComponent lpf_r{};
	bool tempoSync{true};
	bool bufferFull{false};
};

// A plain owner-managed heap block (NOT resource-manager-owned): the GranularProcessor allocates it
// while the FX runs and frees it outright on idle (releaseBuffer). It's never evicted under memory
// pressure, so it carries no Stealable / resource-manager hooks.
class GrainBuffer {
public:
	GrainBuffer() = delete;
	GrainBuffer(GrainBuffer& other) = delete;
	GrainBuffer(const GrainBuffer& other) = delete;
	explicit GrainBuffer(GranularProcessor* grainFX) { owner = grainFX; }
	StereoSample& operator[](int32_t i) { return sampleBuffer[i]; }
	StereoSample operator[](int32_t i) const { return sampleBuffer[i]; }
	bool inUse{true};

private:
	GranularProcessor* owner;
	// Intentionally NOT zero-initialized: this is a large granular FX buffer and
	// zeroing it on every construction is prohibitively expensive. The grain engine
	// writes before it reads, so stale contents are fine here. NOLINT keeps the
	// member-init auto-fix from re-adding `{}` on future audit passes.
	StereoSample sampleBuffer[kModFXGrainBufferSize]; // NOLINT(cppcoreguidelines-pro-type-member-init)
};
