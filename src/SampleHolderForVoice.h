/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#ifndef SAMPLEHOLDERFORVOICE_H_
#define SAMPLEHOLDERFORVOICE_H_

#include "SampleHolder.h"
#include "phaseincrementfinetuner.h"

class Source;

class SampleHolderForVoice final : public SampleHolder {
public:
	SampleHolderForVoice();
	~SampleHolderForVoice();
	void unassignAllClusterReasons(bool beingDestructed = false);
	void setCents(int newCents);
	void recalculateFineTuner();
	void claimClusterReasons(bool reversed, int clusterLoadInstruction = CLUSTER_ENQUEUE);
	void setTransposeAccordingToSamplePitch(bool minimizeOctaves = false, bool doingSingleCycle = false,
	                                        bool rangeCoversJustOneNote = false, bool thatOneNote = 0);
	uint32_t getMSecLimit(Source* source);

	// In samples. 0 means not set
	uint32_t loopStartPos;
	uint32_t loopEndPos; // Unlike endPos, this may not be beyond the waveform ever!

	int16_t transpose;
	int8_t cents;
	PhaseIncrementFineTuner fineTuner;

	Cluster* clustersForLoopStart[NUM_CLUSTERS_LOADED_AHEAD];

	// These two now only exist for loading in data from old files
	uint32_t startMSec;
	uint32_t endMSec;

protected:
	void sampleBeenSet(bool reversed, bool manuallySelected);
};

#endif /* SAMPLEHOLDERFORVOICE_H_ */
