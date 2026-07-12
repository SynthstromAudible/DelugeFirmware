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

class Sound;
class ParamManagerForTimeline;
class WaveTable;
class SampleHolder;
class DxPatch;

namespace deluge::project {
struct DocSource;
}

class Source {
public:
	Source();
	~Source();

	// M3 owner-5 slice G: non-owning pointer into the owning Sound's DocSound (docSound_->sources[s]). A Source is
	// never copied by value (it lives as a fixed member of its Sound), so this pointer has no clone hazard; the Sound
	// binds it at bindDocSound and re-seeds it at doneReadingFromFile. Save reads the record via the Sound's docSound_.
	deluge::project::DocSource* docSource_{nullptr};
	void bindDocSource(deluge::project::DocSource* d);
	void seedDocSourceScalars();

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

	int8_t timeStretchAmount;

	int16_t defaultRangeI; // -1 means none yet

	bool renderInStereo(Sound* s, SampleHolder* sampleHolder = nullptr);
	void setCents(int32_t newCents);
	// M3 owner-5 slice G: setters that write the DocSource record (when bound) alongside the native cache. Every menu /
	// runtime write site funnels through these; save reads the record.
	void setTranspose(int16_t newTranspose);
	void setRepeatMode(SampleRepeatMode newMode);
	void setTimeStretchAmount(int8_t newAmount);
	void setInterpolationMode(InterpolationMode newMode);
	void setPitchAndSpeedIndependent(bool independent);
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
	OscType getOscType();
	void setOscType(OscType newType);

	DxPatch* ensureDxPatch();

private:
};
