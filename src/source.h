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

#ifndef SOURCE_H
#define SOURCE_H

#include <MultiRangeArray.h>
#include "phaseincrementfinetuner.h"
#include "SampleControls.h"

class Sound;
class ParamManagerForTimeline;
class WaveTable;
class SampleHolder;

class Source {
public:
	Source();
	~Source();

	SampleControls sampleControls;

	uint8_t oscType;

	// These are not valid for Samples
	int16_t transpose;
	int8_t cents;
	PhaseIncrementFineTuner fineTuner;

	MultiRangeArray ranges;

	uint8_t repeatMode;

	int8_t timeStretchAmount;

	int16_t defaultRangeI; // -1 means none yet

	bool renderInStereo(SampleHolder* sampleHolder = NULL);
	void setCents(int newCents);
	void recalculateFineTuner();
	int32_t getLengthInSamplesAtSystemSampleRate(int note, bool forTimeStretching = false);
	void detachAllAudioFiles();
	int loadAllSamples(bool mayActuallyReadFiles);
	void setReversed(bool newReversed);
	int getRangeIndex(int note);
	MultiRange* getRange(int note);
	MultiRange* getOrCreateFirstRange();
	bool hasAtLeastOneAudioFileLoaded();
	void doneReadingFromFile(Sound* sound);
	bool hasAnyLoopEndPoint();
	void setOscType(int newType);

private:
	void destructAllMultiRanges();
};

#endif
