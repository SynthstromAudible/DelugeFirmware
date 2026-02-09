/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include "model/sample/sample_controls.h"
#include "storage/multi_range/multi_range_array.h"
#include "util/phase_increment_fine_tuner.h"

namespace deluge::dsp {
struct PhiMorphCache;
}

class Sound;
class ParamManagerForTimeline;
class WaveTable;
class SampleHolder;
class DxPatch;

class Source {
public:
	Source();
	~Source();

	SampleControls sampleControls;

	OscType oscType;

	// These are not valid for Samples
	int16_t transpose;
	int8_t cents;
	PhaseIncrementFineTuner fineTuner;

	MultiRangeArray ranges;

	DxPatch* dxPatch;
	bool dxPatchChanged = false;
	SampleRepeatMode repeatMode;

	// PHI_MORPH zone parameters and cache (lazily allocated)
	uint16_t phiMorphZoneA{0};
	uint16_t phiMorphZoneB{0};
	float phiMorphPhaseOffsetA{0.0f};
	float phiMorphPhaseOffsetB{0.0f};
	float phiMorphGamma{0.0f}; // Shared phase multiplier (push+twist on wave index)
	deluge::dsp::PhiMorphCache* phiMorphCache{nullptr};

	int8_t timeStretchAmount;

	int16_t defaultRangeI; // -1 means none yet

	bool renderInStereo(Sound* s, SampleHolder* sampleHolder = nullptr);
	void setCents(int32_t newCents);
	void recalculateFineTuner();
	int32_t getLengthInSamplesAtSystemSampleRate(int32_t note, bool forTimeStretching = false);
	void detachAllAudioFiles();
	Error loadAllSamples(bool mayActuallyReadFiles);
	void setReversed(bool newReversed);
	int32_t getRangeIndex(int32_t note);
	MultiRange* getRange(int32_t note);
	MultiRange* getOrCreateFirstRange();
	bool hasAtLeastOneAudioFileLoaded();
	void doneReadingFromFile(Sound* sound);
	bool hasAnyLoopEndPoint();
	void setOscType(OscType newType);

	DxPatch* ensureDxPatch();

private:
	void destructAllMultiRanges();
};
