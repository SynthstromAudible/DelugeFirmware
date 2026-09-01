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
#include "gui/l10n/strings.h"
#include "model/drum/choke_group.h"
#include "processing/sound/sound_drum.h"
#include "util/d_string.h"
#include <array>
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
	void abortStemExportProcess(deluge::l10n::String reason);
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
	bool exportChokeGroups;

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

	// export choke groups
	int32_t disarmAllChokeGroupsForStemExport();
	int32_t exportChokeGroupStems(StemExportType stemExportType);
	static uint8_t findBundlingChokeGroups(InstrumentClip* clip, int32_t totalNumNoteRows,
	                                       std::array<bool, deluge::drum::kMaxChokeGroup + 1>& groupBundles);
	static bool chokeGroupIsBundled(const std::array<bool, deluge::drum::kMaxChokeGroup + 1>& groupBundles,
	                                uint8_t chokeGroup);
	static bool armChokeGroupRows(InstrumentClip* clip, int32_t totalNumNoteRows, uint8_t group,
	                              int32_t* groupLoopLength);
	static void muteChokeGroupRows(InstrumentClip* clip, int32_t totalNumNoteRows, uint8_t group);
	static int32_t noteRowLoopLength(InstrumentClip* clip, NoteRow* noteRow);
	bool recordOneChokeGroupStem(StemExportType stemExportType, InstrumentClip* clip, Output* output, SoundDrum* drum,
	                             uint8_t chokeGroup, int32_t fileIndex, int32_t loopLength);
	int32_t exportBundledChokeGroups(StemExportType stemExportType, InstrumentClip* clip, Output* output,
	                                 int32_t totalNumNoteRows,
	                                 const std::array<bool, deluge::drum::kMaxChokeGroup + 1>& groupBundles,
	                                 int32_t fileIndex);
	int32_t exportUngroupedDrums(StemExportType stemExportType, InstrumentClip* clip, Output* output,
	                             int32_t totalNumNoteRows,
	                             const std::array<bool, deluge::drum::kMaxChokeGroup + 1>& groupBundles,
	                             int32_t fileIndex);
	/// read-only check for whether choke-group export would actually produce more than one file for
	/// the kit clip currently open - used to decide whether to surface the option in the UI at all.
	/// Unlike disarmAllChokeGroupsForStemExport(), this does not touch NoteRow mute state.
	bool currentKitHasBundlingChokeGroup();

	// start exporting
	/// chokeGroup is only meaningful for StemExportType::CHOKE_GROUP, where it names the group in the
	/// file name. It is deliberately separate from fileNumber: a group export still counts its files
	/// from 000 like every other export type, and the scroll position must not be driven by a group
	/// number that has nothing to do with any row.
	bool startCurrentStemExport(StemExportType stemExportType, Output* output, bool& muteState, int32_t fileNumber,
	                            bool exportStem, SoundDrum* drum = nullptr, uint8_t chokeGroup = 0);

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
	/// returns false if no valid file name could be built (e.g. the output / drum names are too long
	/// for the file system), in which case the stem must not be exported
	[[nodiscard]] bool setWavFileNameForStemExport(StemExportType type, Output* output, int32_t fileNumber,
	                                               SoundDrum* drum = nullptr, uint8_t chokeGroup = 0);
	String wavFileNameForStemExport;
	bool wavFileNameForStemExportSet;

	// check if we're in context menu
	bool inContextMenu();
	bool renderingOffline() { return processStarted && renderOffline; }
};

extern StemExport stemExport;
