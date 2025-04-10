/*
 * Copyright (c) 2024 Sean Ditny
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
#include "processing/sound/sound_drum.h"
#include "util/d_string.h"
#include <cstdint>

class Output;
class Clip;
class SoundDrum;

class StemExport {
public:
	StemExport();

	// start & stop process
	void startStemExportProcess(StemExportType stemExportType);
	void stopStemExportProcess();
	void startOutputRecordingUntilLoopEndAndSilence();
	void stopPlayback();
	void stopOutputRecording();
	bool checkForLoopEnd();
	bool checkForSilence();
	bool processStarted;
	bool stopRecording;
	StemExportType currentStemExportType;
	uint32_t timePlaybackStopped;
	uint32_t timeThereWasLastSomeActivity;

	// export config variables
	bool allowNormalization;
	bool allowNormalizationForDrums;
	bool exportToSilence;
	bool includeSongFX;
	bool includeKitFX;
	bool renderOffline;
	bool exportMixdown;

	// export instruments
	int32_t disarmAllInstrumentsForStemExport(StemExportType stemExportType);
	int32_t exportInstrumentStems(StemExportType stemExportType);
	int32_t exportMixdownStem(StemExportType stemExportType);
	void restoreAllInstrumentMutes(int32_t totalNumOutputs);

	// export clips
	int32_t disarmAllClipsForStemExport();
	int32_t exportClipStems(StemExportType stemExportType);
	void restoreAllClipMutes(int32_t totalNumClips);
	void getLoopLengthOfLongestNotEmptyNoteRow(Clip* clip);
	void getLoopEndPointInSamplesForAudioFile(int32_t loopLength);
	bool writeLoopEndPos();
	int32_t loopLengthToStopStemExport;
	int32_t loopEndPointInSamplesForAudioFile;

	// export drums
	int32_t disarmAllDrumsForStemExport();
	int32_t exportDrumStems(StemExportType stemExportType);
	void restoreAllDrumMutes(int32_t totalNumNoteRows);

	// start exporting
	bool startCurrentStemExport(StemExportType stemExportType, Output* output, bool& muteState, int32_t fileNumber,
	                            bool exportStem, SoundDrum* drum = nullptr);

	// finish exporting
	void finishCurrentStemExport(StemExportType stemExportType, bool& muteState);
	void finishStemExportProcess(StemExportType stemExportType, int32_t elementsProcessed);
	void updateScrollPosition(StemExportType stemExportType, int32_t indexNumber);

	// export status
	void displayStemExportProgress(StemExportType stemExportType);
	void displayStemExportProgressOLED(StemExportType stemExportType);
	void displayStemExportProgress7SEG();
	int32_t numStemsExported;
	int32_t totalNumStemsToExport;

	// audio file management
	Error getUnusedStemRecordingFilePath(String* filePath, AudioRecordingFolder folder);
	Error getUnusedStemRecordingFolderPath(String* filePath, AudioRecordingFolder folder);
	int32_t highestUsedStemFolderNumber;
	String lastFolderNameForStemExport;
	void setWavFileNameForStemExport(StemExportType type, Output* output, int32_t fileNumber,
	                                 SoundDrum* drum = nullptr);
	String wavFileNameForStemExport;
	bool wavFileNameForStemExportSet;

	// check if we're in context menu
	bool inContextMenu();
};

extern StemExport stemExport;
