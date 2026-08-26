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
#include "processing/sound/sound_drum.h"
#include "util/c_string.h"
#include <cstdint>
#include <string>

class Output;
class Clip;
class SoundDrum;

class StemExport {
public:
	StemExport();

	// start & stop process
	void startStemExportProcess(StemExportType stemExportType);
	// The actual export work, run on the worker fiber (it yields per element). Public
	// only so the worker dispatch lambda in startStemExportProcess can reach it.
	void runStemExportProcess(StemExportType stemExportType);
	void stopStemExportProcess();
	void abortStemExportProcess(deluge::l10n::String reason);
	void startOutputRecordingUntilLoopEndAndSilence();
	void stopPlayback();
	void stopOutputRecording();
	// Wait until `until` is satisfied while the export renders. On-device this is a
	// cooperative scheduler yield() (UI/MIDI/etc must keep running); for an offline
	// (faster-than-realtime) render it drives AudioEngine::routine() directly in a tight
	// loop, bypassing the scheduler — whose chooseBestTask busy-wait otherwise caps the
	// headless export at ~realtime. `until` matches yield()'s RunCondition (bool(*)()).
	void renderWait(bool (*until)());
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

	/// @brief One element of an in-flight export: whichever of output, clip or note row the current
	///        export type walks.
	///
	/// Produced by @ref fetchElement from an index rather than held across the element's recording,
	/// because the export suspends there and a pointer taken beforehand would have to be revalidated.
	struct StemElement {
		Output* output = nullptr;
		/// @brief The element's own mute flag, written by @ref startElement and @ref finishElement.
		///
		/// Which flag it is depends on the export type: the arranger mute, the clip's active state, or
		/// the note row's mute. Null for MIXDOWN, which records everything together and mutes nothing.
		bool* muteState = nullptr;
		bool exportStem = false;
		SoundDrum* drum = nullptr;
	};

	/// @brief Walks every element of the current export type, recording one stem each.
	/// @return The element count, which the caller uses to restore the scroll position.
	int32_t exportStems();

	/// @brief Counts the elements this export type walks and disarms them for recording.
	/// @return The element count, as @ref restoreElements and the caller both expect it.
	int32_t prepareElements();

	/// @brief Fetches element `index`, applying whatever per-type setup it needs (loop lengths, unmutes).
	/// @return false if that slot is empty, in which case the element is skipped.
	bool fetchElement(int32_t index, StemElement& element);

	/// @brief Starts recording one element.
	/// @return false if the element is skipped, or if the export was aborted while starting it - the
	///         two are told apart by whether UI_MODE_STEM_EXPORT is still active.
	bool startElement(int32_t index, const StemElement& element);

	/// @brief Mutes the element again and counts it. Runs once its recording has finished.
	void finishElement(const StemElement& element);

	/// @brief The wait every export type shares: this element's recording has finished and playback
	///        has stopped. Matches renderWait()'s RunCondition.
	static bool elementFinished();

	/// @brief Restores whatever mutes @ref prepareElements changed.
	void restoreElements(int32_t total);

	// export instruments
	int32_t disarmAllInstrumentsForStemExport(StemExportType stemExportType);
	void restoreAllInstrumentMutes(int32_t totalNumOutputs);

	// export clips
	int32_t disarmAllClipsForStemExport();
	void restoreAllClipMutes(int32_t totalNumClips);
	void getLoopLengthOfLongestNotEmptyNoteRow(Clip* clip);
	void getLoopEndPointInSamplesForAudioFile(int32_t loopLength);
	bool writeLoopEndPos();
	int32_t loopLengthToStopStemExport;
	int32_t loopEndPointInSamplesForAudioFile;

	// export drums
	int32_t disarmAllDrumsForStemExport();
	void restoreAllDrumMutes(int32_t totalNumNoteRows);

	/// @brief Clip count captured by a DRUM export's own clip disarm, so @ref restoreElements can undo
	///        it as well as the note-row mutes.
	int32_t drumExportTotalNumClips;

	// start exporting
	bool startCurrentStemExport(StemExportType stemExportType, Output* output, bool* muteState, int32_t fileNumber,
	                            bool exportStem, SoundDrum* drum = nullptr);

	// finish exporting
	void finishCurrentStemExport(StemExportType stemExportType, bool* muteState);
	void finishStemExportProcess(StemExportType stemExportType, int32_t elementsProcessed);
	void updateScrollPosition(StemExportType stemExportType, int32_t indexNumber);

	// export status
	void displayStemExportProgress(StemExportType stemExportType);
	int32_t numStemsExported;
	int32_t totalNumStemsToExport;

	// audio file management
	Error getUnusedStemRecordingFilePath(std::string* filePath, AudioRecordingFolder folder);
	Error getUnusedStemRecordingFolderPath(std::string* filePath, AudioRecordingFolder folder);
	int32_t highestUsedStemFolderNumber;
	std::string lastFolderNameForStemExport{};
	/// returns false if no valid file name could be built (e.g. the output / drum names are too long
	/// for the file system), in which case the stem must not be exported
	[[nodiscard]] bool setWavFileNameForStemExport(StemExportType type, Output* output, int32_t fileNumber,
	                                               SoundDrum* drum = nullptr);
	std::string wavFileNameForStemExport{};
	bool wavFileNameForStemExportSet;

	// check if we're in context menu
	bool inContextMenu();
	bool renderingOffline() { return processStarted && renderOffline; }
};

extern StemExport stemExport;
