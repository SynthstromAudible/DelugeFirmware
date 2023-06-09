/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

#ifndef AUDIOFILEHOLDER_H_
#define AUDIOFILEHOLDER_H_

#include "definitions.h"
#include "DString.h"

extern "C" {
#include "fatfs/ff.h"
}

class Sample;
class AudioFile;

class AudioFileHolder {
public:
	AudioFileHolder();
	virtual ~AudioFileHolder();
	virtual void setAudioFile(AudioFile* newSample, bool reversed = false, bool manuallySelected = false,
	                          int clusterLoadInstruction = CLUSTER_ENQUEUE);
	int loadFile(bool reversed, bool manuallySelected, bool mayActuallyReadFile,
	             int clusterLoadInstruction = CLUSTER_ENQUEUE, FilePointer* filePointer = NULL,
	             bool makeWaveTableWorkAtAllCosts = false);
	virtual void unassignAllClusterReasons(bool beingDestructed = false) {}

	String filePath;
	AudioFile* audioFile;
	uint8_t audioFileType;
};

#endif /* AUDIOFILEHOLDER_H_ */
