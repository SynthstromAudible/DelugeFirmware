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

#include "processing/stem_export/stem_search_policy.h"

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
	void stopStemExportProcess();
	void abortStemExportProcess(deluge::l10n::String reason);
	void startOutputRecordingUntilLoopEndAndSilence();
	void stopPlayback();
	void stopOutputRecording();

	/// @brief Advances an in-flight export. Cheap no-op when none is running.
	///
	/// Registered as a repeating task by registerTasks(). The export used to run as one long call that
	/// blocked per element, which meant nothing else could run while it did - not the UI that has to
	/// service a cancellation, and not the fill task the render depends on. Now each step returns here.
	void exportRoutine();

	/// @brief True from @ref startStemExportProcess until the export has torn down.
	[[nodiscard]] bool isExportInFlight() const { return exportPhase != ExportPhase::NONE; }
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

	/// @brief Where an in-flight export is parked between run-loop passes.
	///
	/// Only the per-element wait can span passes; each step itself runs straight through on the storage
	/// owner. RUNNING covers a step that is queued or executing, and stops @ref exportRoutine
	/// dispatching a second on top of it.
	enum class ExportPhase : uint8_t {
		NONE,          ///< No export in flight.
		RUNNING,       ///< A step is queued on, or running on, the storage owner.
		NEXT_ELEMENT,  ///< Ready to start the next element.
		AWAIT_ELEMENT, ///< The current element is recording; waiting for it to finish.
		FINISH,        ///< Every element done, or the user cancelled: tear down.
	};
	ExportPhase exportPhase{ExportPhase::NONE};

	/// @name The export's cursor, promoted from exportStems()' locals because they span the wait.
	/// @{
	int32_t exportTotal;       ///< Element count from prepareElements(); what the teardown restores.
	int32_t exportIndex;       ///< The element being walked, counting down.
	StemElement exportElement; ///< The element @ref exportIndex resolved to.

	/// @brief Parked at the offline render's drain point, between the audio routine and the slow
	///        routines - exactly where the blocking drain used to suspend the worker fiber.
	///
	/// The rendered audio depends on that ordering: the drain decides cluster eviction and underrun
	/// behaviour, so moving it even one call earlier or later changes the output bit-for-bit (see
	/// AudioEngine::routine()'s offline batch comment). Resuming mid-iteration is what keeps it put.
	bool exportAwaitingDrain;
	/// @}

	/// @name The export's steps, each dispatched onto the storage owner. Owner-only.
	/// @{
	void startNextElement(); ///< Fetch and start the next element, or fall through to the teardown.
	void driveElement();     ///< One bounded slice of an offline render, then re-check for the finish.
	void finishExport();     ///< Restore, run the completion UI, and clear the export.
	/// @}

	/// @name `Owner::run` trampolines for the three steps above.
	/// @{
	static void startNextElementTrampoline(void*);
	static void driveElementTrampoline(void*);
	static void finishExportTrampoline(void*);
	/// @}

	/// @brief Queues the next step on the storage owner, staying parked if the queue is full.
	void dispatchExportStep(void (*step)(void*));

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

	/// @name Folder-search machine state
	/// Cross-tick state for the free-folder search. The loop it replaces held these on the stack and
	/// blocked until it finished; now it issues one operation per tick and returns.
	/// @{
	deluge::storage::StemSearchPhase stemSearchPhase_ = deluge::storage::StemSearchPhase::MkdirBase;
	bool stemSearchIssued_ = false;
	bool stemSearchActive_ = false;
	std::string stemSearchBasePath_{};
	std::string stemSearchCandidatePath_{};
	int32_t stemSearchCandidatesTried_ = 0;
	/// Consecutive calls the current candidate's channel operation has gone unanswered.
	int32_t stemSearchStallTicks_ = 0;
	/// @}
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
