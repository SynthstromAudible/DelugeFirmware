/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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
#include "memory/stealable.h"
#include <cstdint>

class Sample;
class SampleCluster;
class SampleCache;

// Please see explanation of Clusters and SD card streaming at the top of AudioFileManager.h

class Cluster final : public Stealable {
public:
	Cluster();
	void convertDataIfNecessary();
	bool mayBeStolen(void* thingNotToStealFrom);
	void steal(char const* errorCode);
	StealableQueue getAppropriateQueue();

	ClusterType type;
	int8_t numReasonsHeldBySampleRecorder;
	bool extraBytesAtStartConverted;
	bool extraBytesAtEndConverted;
	int32_t numReasonsToBeLoaded;
	Sample* sample;
	uint32_t clusterIndex;
	SampleCache* sampleCache;
	char firstThreeBytesPreDataConversion[3];
	bool loaded;

	char dummy[CACHE_LINE_SIZE];

	char data[CACHE_LINE_SIZE];
};
