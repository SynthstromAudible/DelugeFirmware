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
#include "util/d_string.h"
#include <cstdint>

class Output;

class StemExport {
public:
	StemExport();

	// start & stop process
	void startStemExportProcess(StemExportType stemExportType);
	void stopStemExportProcess();
	void startOutputRecordingUntilLoopEnd();
	void stopOutputRecordingAndPlayback();
	bool processStarted;

	// export instruments
	void disarmAllInstrumentsForStemExport();
	void exportInstrumentStems();

	// export clips
	void disarmAllClipsForStemExport();
	void exportClipStems();

	// finish exporting
	void finishStemExportProcess(StemExportType stemExportType);

	// export status
	void displayStemExportProgress(StemExportType stemExportType);
	int32_t numStemsExported;
	int32_t totalNumStemsToExport;

	// audio file management
	Error getUnusedStemRecordingFilePath(String* filePath, AudioRecordingFolder folder);
	int32_t highestUsedStemFolderNumber;
	String lastSongNameForStemExport;
	void setWavFileNameForStemExport(StemExportType type, Output* output, int32_t fileNumber);
	String wavFileNameForStemExport;
	bool wavFileNameForStemExportSet;

	// check if we're in context menu
	bool inContextMenu();
};

extern StemExport stemExport;
